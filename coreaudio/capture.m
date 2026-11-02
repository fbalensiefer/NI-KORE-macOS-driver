/* SPDX-License-Identifier: GPL-2.0-or-later
 * Direct HAL capture: preallocated RAM in IOProc, files only after device stop.
 * No default-device, sample-rate, gain or buffer-size changes.
 */
#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreAudio/HostTime.h>
#include <stdatomic.h>
#include <signal.h>
#include <math.h>
static volatile sig_atomic_t interrupted;
static void stop_signal(int sig){(void)sig;interrupted=1;}
struct Capture {
    float *samples;
    unsigned channels,rate;
    uint64_t capacity,stored,callbacks,observed,gaps,invalid,timestamped;
    double firstSample,lastSample,expectedSample,maxGap;
    uint64_t firstHost,lastHost;
    _Atomic int done;
};
static OSStatus capture_io(AudioObjectID device,const AudioTimeStamp *now,const AudioBufferList *input,
    const AudioTimeStamp *time,AudioBufferList *output,const AudioTimeStamp *outTime,void *context) {
    (void)device;(void)now;(void)output;(void)outTime;
    struct Capture *c=context;
    if(atomic_load_explicit(&c->done,memory_order_relaxed))return noErr;
    unsigned channels=0,frames=0;
    for(UInt32 b=0;b<input->mNumberBuffers;b++) {
        const AudioBuffer *buffer=&input->mBuffers[b];
        if(!buffer->mNumberChannels || !buffer->mData || buffer->mDataByteSize%(4*buffer->mNumberChannels)) {
            c->invalid++;atomic_store(&c->done,1);return noErr;
        }
        unsigned n=buffer->mDataByteSize/(4*buffer->mNumberChannels);
        if(b && n!=frames){c->invalid++;atomic_store(&c->done,1);return noErr;}
        frames=n;channels+=buffer->mNumberChannels;
    }
    if(!frames || channels!=c->channels){c->invalid++;atomic_store(&c->done,1);return noErr;}
    BOOL valid=(time->mFlags&kAudioTimeStampSampleTimeValid) && (time->mFlags&kAudioTimeStampHostTimeValid);
    if(valid) {
        if(c->timestamped) {
            double gap=time->mSampleTime-c->expectedSample;
            if(fabs(gap)>.5){c->gaps++;if(fabs(gap)>c->maxGap)c->maxGap=fabs(gap);}
        } else {c->firstSample=time->mSampleTime;c->firstHost=time->mHostTime;}
        c->lastSample=time->mSampleTime;c->lastHost=time->mHostTime;
        c->expectedSample=time->mSampleTime+frames;c->timestamped++;
    } else c->invalid++;
    unsigned keep=(unsigned)MIN((uint64_t)frames,c->capacity-c->stored),offset=0;
    for(UInt32 b=0;b<input->mNumberBuffers;b++) {
        const AudioBuffer *buffer=&input->mBuffers[b];const float *data=buffer->mData;
        for(unsigned n=0;n<keep;n++)for(unsigned ch=0;ch<buffer->mNumberChannels;ch++)
            c->samples[(c->stored+n)*c->channels+offset+ch]=data[n*buffer->mNumberChannels+ch];
        offset+=buffer->mNumberChannels;
    }
    c->stored+=keep;c->observed+=frames;c->callbacks++;
    if(c->stored==c->capacity)atomic_store_explicit(&c->done,1,memory_order_release);
    return noErr;
}
static id property(AudioDeviceID device,AudioObjectPropertySelector selector) {
    CFStringRef string=NULL;UInt32 size=sizeof(string);
    AudioObjectPropertyAddress address={selector,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    if(AudioObjectGetPropertyData(device,&address,0,NULL,&size,&string))return nil;
    return CFBridgingRelease(string);
}
static unsigned input_channels(AudioDeviceID device) {
    AudioObjectPropertyAddress a={kAudioDevicePropertyStreamConfiguration,kAudioDevicePropertyScopeInput,kAudioObjectPropertyElementMain};
    UInt32 size=0;if(AudioObjectGetPropertyDataSize(device,&a,0,NULL,&size) || !size)return 0;
    AudioBufferList *list=calloc(1,size);unsigned channels=0;
    if(!AudioObjectGetPropertyData(device,&a,0,NULL,&size,list))
        for(UInt32 i=0;i<list->mNumberBuffers;i++)channels+=list->mBuffers[i].mNumberChannels;
    free(list);return channels;
}
static NSArray *devices(void) {
    AudioObjectPropertyAddress a={kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    UInt32 size=0;if(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&a,0,NULL,&size))return @[];
    AudioDeviceID *ids=malloc(size);if(!ids)return @[];NSMutableArray *result=[NSMutableArray array];
    if(!AudioObjectGetPropertyData(kAudioObjectSystemObject,&a,0,NULL,&size,ids))
        for(unsigned i=0;i<size/sizeof(*ids);i++) {
            unsigned ch=input_channels(ids[i]);
            if(ch)[result addObject:@{@"id":@(ids[i]),@"name":property(ids[i],kAudioObjectPropertyName)?:@"",@"uid":property(ids[i],kAudioDevicePropertyDeviceUID)?:@"",@"input_channels":@(ch)}];
        }
    free(ids);return result;
}
static void u16(FILE *f,uint16_t v){unsigned char p[2]={v&255,v>>8};fwrite(p,1,2,f);}
static void u32(FILE *f,uint32_t v){unsigned char p[4]={v&255,(v>>8)&255,(v>>16)&255,v>>24};fwrite(p,1,4,f);}
static BOOL write_wave(const char *path,struct Capture *c) {
    FILE *f=fopen(path,"wx");if(!f){perror("capture output");return NO;}
    uint32_t bytes=(uint32_t)(c->stored*c->channels*4);
    fwrite("RIFF",1,4,f);u32(f,36+bytes);fwrite("WAVEfmt ",1,8,f);u32(f,16);u16(f,1);u16(f,c->channels);
    u32(f,c->rate);u32(f,c->rate*c->channels*4);u16(f,c->channels*4);u16(f,32);fwrite("data",1,4,f);u32(f,bytes);
    for(uint64_t i=0;i<c->stored*c->channels;i++) {
        double v=c->samples[i];if(!isfinite(v))v=0;if(v>1)v=1;if(v< -1)v= -1;
        int32_t sample=(int32_t)(v*2147483647.);u32(f,(uint32_t)sample);
    }
    BOOL ok=!ferror(f);if(fclose(f))ok=NO;return ok;
}
int main(int argc,const char **argv) {@autoreleasepool {
    const char *output=NULL,*report=NULL;NSString *name=@"MAYA44 USB+",*uid=nil;unsigned seconds=10;
    BOOL list=NO;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--list"))list=YES;
        else if(!strcmp(argv[i],"--record") && i+1<argc)output=argv[++i];
        else if(!strcmp(argv[i],"--report") && i+1<argc)report=argv[++i];
        else if(!strcmp(argv[i],"--uid") && i+1<argc)uid=@(argv[++i]);
        else if(!strcmp(argv[i],"--seconds") && i+1<argc){char *end;unsigned long n=strtoul(argv[++i],&end,10);if(*end || n<1 || n>10)return 2;seconds=(unsigned)n;}
        else {fprintf(stderr,"Usage: %s --list | --record WAV --report JSON [--uid UID] [--seconds 1..10]\n",argv[0]);return 2;}
    }
    NSArray *all=devices();NSError *error=nil;
    if(list || !output){puts([[NSString alloc] initWithData:[NSJSONSerialization dataWithJSONObject:all options:NSJSONWritingPrettyPrinted error:&error] encoding:NSUTF8StringEncoding].UTF8String);return 0;}
    if(!report || access(output,F_OK)==0 || access(report,F_OK)==0){fprintf(stderr,"Need new WAV and report paths\n");return 2;}
    NSDictionary *selected=nil;unsigned matches=0;
    for(NSDictionary *entry in all)if(uid?[entry[@"uid"] isEqual:uid]:[entry[@"name"] isEqual:name]){selected=entry;matches++;}
    if(matches!=1){fprintf(stderr,"Need exactly one selected capture device, found %u\n",matches);return 1;}
    AudioDeviceID device=[selected[@"id"] unsignedIntValue];
    AudioObjectPropertyAddress a={kAudioDevicePropertyStreamFormat,kAudioDevicePropertyScopeInput,kAudioObjectPropertyElementMain};
    AudioStreamBasicDescription format={0};UInt32 size=sizeof(format);
    OSStatus status=AudioObjectGetPropertyData(device,&a,0,NULL,&size,&format);
    if(status || format.mFormatID!=kAudioFormatLinearPCM || !(format.mFormatFlags&kAudioFormatFlagIsFloat) || format.mBitsPerChannel!=32 || (format.mFormatFlags&kAudioFormatFlagIsBigEndian) || format.mSampleRate<8000 || format.mSampleRate>192000) {
        fprintf(stderr,"Unsupported HAL input format status=%d rate=%.1f flags=%u bits=%u\n",status,format.mSampleRate,(unsigned)format.mFormatFlags,(unsigned)format.mBitsPerChannel);return 1;
    }
    struct Capture capture={0};capture.channels=[selected[@"input_channels"] unsignedIntValue];capture.rate=(unsigned)format.mSampleRate;
    capture.capacity=(uint64_t)capture.rate*seconds;
    if(!capture.channels || capture.channels>32)return 1;
    capture.samples=calloc(capture.capacity*capture.channels,sizeof(float));if(!capture.samples)return 1;
    AudioDeviceIOProcID io=NULL;status=AudioDeviceCreateIOProcID(device,capture_io,&capture,&io);
    if(status){fprintf(stderr,"Create IOProc failed %d\n",status);free(capture.samples);return 1;}
    signal(SIGINT,stop_signal);signal(SIGTERM,stop_signal);
    status=AudioDeviceStart(device,io);setvbuf(stdout,NULL,_IOLBF,0);
    if(status)fprintf(stderr,"AudioDeviceStart failed %d\n",status);
    if(!status)printf("CAPTURE READY uid=%s rate=%u channels=%u target_frames=%llu\n",[selected[@"uid"] UTF8String],capture.rate,capture.channels,(unsigned long long)capture.capacity);
    uint64_t limit=AudioGetCurrentHostTime()+AudioConvertNanosToHostTime((uint64_t)(seconds+5)*1000000000);
    while(!status && !interrupted && !atomic_load_explicit(&capture.done,memory_order_acquire) && AudioGetCurrentHostTime()<limit)usleep(10000);
    OSStatus stop=AudioDeviceStop(device,io),destroy=AudioDeviceDestroyIOProcID(device,io);
    if(stop || destroy) {fprintf(stderr,"HAL stop/destroy failed %d/%d; retaining callback storage until exit\n",stop,destroy);fflush(NULL);_Exit(1);}
    BOOL complete=!status && !capture.invalid && !capture.gaps && capture.stored==capture.capacity;
    double span=AudioConvertHostTimeToNanos(capture.lastHost-capture.firstHost)/1e9;
    double measuredRate=span>0?(capture.lastSample-capture.firstSample)/span:0;
    NSDictionary *result=@{@"device":selected,@"nominal_rate":@(format.mSampleRate),@"channels":@(capture.channels),
        @"stored_frames":@(capture.stored),@"observed_frames":@(capture.observed),@"target_frames":@(capture.capacity),
        @"callbacks":@(capture.callbacks),@"timestamped_callbacks":@(capture.timestamped),@"timestamp_gaps":@(capture.gaps),
        @"invalid_callbacks":@(capture.invalid),@"max_gap_frames":@(capture.maxGap),@"sample_duration_s":@((double)capture.stored/capture.rate),
        @"timestamp_span_s":@(span),@"host_measured_rate":@(measuredRate),@"capture_contiguous":@(complete),
        @"start_status":@(status),@"stop_status":@(stop),@"destroy_status":@(destroy)};
    BOOL waveOK=write_wave(output,&capture);NSData *json=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:&error];
    BOOL jsonOK=json && [json writeToFile:@(report) options:NSDataWritingWithoutOverwriting error:&error];
    puts([[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding].UTF8String);free(capture.samples);
    return complete && waveOK && jsonOK?0:1;
}}
