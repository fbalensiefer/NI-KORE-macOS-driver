/* SPDX-License-Identifier: GPL-2.0-or-later
 * Explicit test of the virtual KORE device only, never the default output.
 * USB gain is independently bounded: default .00025, explicit test ceiling .001.
 */
#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreMIDI/CoreMIDI.h>
#include <stdatomic.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include "client.h"
#include "test_signal.h"
struct Render {uint64_t frames;int stereo;_Atomic int done,failed;};
static OSStatus render(AudioDeviceID id,const AudioTimeStamp *now,const AudioBufferList *in,const AudioTimeStamp *it,AudioBufferList *out,const AudioTimeStamp *ot,void *ctx) {
    (void)id;(void)now;(void)in;(void)it;(void)ot;struct Render *r=ctx;
    if(!out || !out->mNumberBuffers){atomic_store(&r->failed,1);atomic_store(&r->done,1);return 0;}
    unsigned frames=0,channels=0;
    for(UInt32 b=0;b<out->mNumberBuffers;b++) {
        AudioBuffer *buffer=&out->mBuffers[b];
        if(!buffer->mData || !buffer->mNumberChannels || buffer->mDataByteSize%(4*buffer->mNumberChannels)) {
            atomic_store(&r->failed,1);atomic_store(&r->done,1);return 0;
        }
        unsigned n=buffer->mDataByteSize/(4*buffer->mNumberChannels);
        if(b && n!=frames){atomic_store(&r->failed,1);atomic_store(&r->done,1);return 0;}
        frames=n;channels+=buffer->mNumberChannels;
        memset(buffer->mData,0,buffer->mDataByteSize);
    }
    if(channels!=2){atomic_store(&r->failed,1);atomic_store(&r->done,1);return 0;}
    if(atomic_load(&r->done))return 0;
    for(unsigned n=0;n<frames;n++) {
        float value[2];kore_test_signal(r->frames+n,r->stereo,value);
        unsigned channel=0;
        for(UInt32 b=0;b<out->mNumberBuffers;b++) {
            float *values=out->mBuffers[b].mData;
            for(unsigned ch=0;ch<out->mBuffers[b].mNumberChannels;ch++,channel++)
                values[n*out->mBuffers[b].mNumberChannels+ch]=value[channel];
        }
    }
    r->frames+=frames;if(r->frames>=(r->stereo?KORE_STEREO_TEST_FRAMES:120000))atomic_store(&r->done,1);return 0;
}
static int send_feedback_test(void) {
    MIDIEndpointRef destination=0;
    for(ItemCount i=0;i<MIDIGetNumberOfDestinations();i++) {
        MIDIEndpointRef candidate=MIDIGetDestination(i);CFStringRef name=NULL;
        if(!MIDIObjectGetStringProperty(candidate,kMIDIPropertyName,&name) && name) {
            BOOL match=CFEqual(name,CFSTR("KORE 1 Feedback"));CFRelease(name);if(match){destination=candidate;break;}
        }
    }
    if(!destination){fprintf(stderr,"Feedback destination not found\n");return 1;}
    MIDIClientRef client=0;MIDIPortRef port=0;
    OSStatus status=MIDIClientCreate(CFSTR("KORE bounded feedback test"),NULL,NULL,&client);
    if(!status)status=MIDIOutputPortCreate(client,CFSTR("LCD test"),&port);
    const unsigned char text[]={0xf0,0x7d,0x4b,1,0,1,'S','Y','S','T','E','M',' ','A','U','D','I','O',' ','T','E','S','T',0xf7};
    MIDIPacketList list;MIDIPacket *packet=MIDIPacketListInit(&list);
    if(!MIDIPacketListAdd(&list,sizeof(list),packet,0,sizeof(text),text))status=-1;
    if(!status)status=MIDISend(port,destination,&list);
    if(port)MIDIPortDispose(port);if(client)MIDIClientDispose(client);
    printf("MIDI feedback test status=%d\n",status);return status?1:0;
}
int main(int argc,const char **argv) {@autoreleasepool {
    BOOL run=NO,feedback=NO,stereo=NO;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--run"))run=YES;
        else if(!strcmp(argv[i],"--feedback"))feedback=YES;
        else if(!strcmp(argv[i],"--stereo-test"))stereo=YES;
        else {fprintf(stderr,"Usage: %s [--run] [--feedback] [--stereo-test]\n",argv[0]);return 2;}
    }
    if(!run){puts("Dry run: --run sends a bounded signal exclusively to the KORE virtual output");return 0;}
    AudioDeviceID device=kore_coreaudio_find();if(!device){fprintf(stderr,"KORE virtual output not installed\n");return 1;}
    AudioStreamBasicDescription format={0};UInt32 size=sizeof(format);
    AudioObjectPropertyAddress a={kAudioDevicePropertyStreamFormat,kAudioObjectPropertyScopeOutput,kAudioObjectPropertyElementMain};
    if(AudioObjectGetPropertyData(device,&a,0,NULL,&size,&format) || format.mSampleRate!=48000 || format.mChannelsPerFrame!=2 || format.mBitsPerChannel!=32 || !(format.mFormatFlags&kAudioFormatFlagIsFloat))return 1;
    struct Render state={.stereo=stereo};AudioDeviceIOProcID proc=NULL;OSStatus status=AudioDeviceCreateIOProcID(device,render,&state,&proc);
    if(status)return 1;status=AudioDeviceStart(device,proc);
    if(!status && feedback && send_feedback_test())status=kAudioHardwareUnspecifiedError;
    for(unsigned i=0;!status && !atomic_load(&state.done) && i<(stereo?650:350);i++)usleep(10000);
    OSStatus stopped=AudioDeviceStop(device,proc),destroyed=AudioDeviceDestroyIOProcID(device,proc);
    if(stopped || destroyed){fprintf(stderr,"Render teardown failed %d/%d\n",stopped,destroyed);fflush(NULL);_Exit(1);}
    printf("Virtual output render frames=%llu stereo=%d status=%d\n",(unsigned long long)state.frames,stereo,status);
    return status || atomic_load(&state.failed) || !atomic_load(&state.done)?1:0;
}}
