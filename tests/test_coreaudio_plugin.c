/* Exercise the real plugin ABI and PCM packer, not a simulated property model. */
#include "../coreaudio/plugin.c"
#include "../coreaudio/pcm_source.h"
#include <assert.h>
#include <stdio.h>
static void *produce(void *context) {
    struct kore_pcm_ring *q=context;
    for(unsigned i=0;i<100000;i++) {float f[2]={(float)i,-(float)i};while(!kore_pcm_push(q,f,1)){} }
    return NULL;
}
int main(void) {
    assert(KoreAudioFactory(NULL,kAudioServerPlugInTypeUUID)==driverRef);
    assert(initialize(driverRef,NULL)==0);
    AudioObjectPropertyAddress a={kAudioDevicePropertyStreams,kAudioObjectPropertyScopeOutput,kAudioObjectPropertyElementMain};
    UInt32 size=0,used=0,value=0;assert(!get_size(driverRef,KORE_DEVICE,1,&a,0,NULL,&size) && size==4);
    assert(!get(driverRef,KORE_DEVICE,1,&a,0,NULL,4,&used,&value) && value==KORE_STREAM);
    a.mScope=kAudioObjectPropertyScopeInput;assert(!get_size(driverRef,KORE_DEVICE,1,&a,0,NULL,&size) && !size);
    a=(AudioObjectPropertyAddress){kAudioStreamPropertyVirtualFormat,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    AudioStreamBasicDescription f;assert(!get(driverRef,KORE_STREAM,1,&a,0,NULL,sizeof(f),&used,&f) && f.mSampleRate==48000 && f.mChannelsPerFrame==2);
    a=(AudioObjectPropertyAddress){kAudioPlugInPropertyBoxList,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    assert(!get(driverRef,kAudioObjectPlugInObject,1,&a,0,NULL,4,&used,&value) && value==KORE_BOX);
    a.mSelector=kAudioBoxPropertyDeviceList;
    assert(!get(driverRef,KORE_BOX,1,&a,0,NULL,4,&used,&value) && value==KORE_DEVICE);
    a.mSelector=kAudioBoxPropertyAcquired;
    assert(!get(driverRef,KORE_BOX,1,&a,0,NULL,4,&used,&value) && value==1);
    for(unsigned object=KORE_DEVICE;object<=KORE_BOX;object++) {
        a.mSelector=kAudioObjectPropertyModelName;CFStringRef name=NULL;
        assert(!get_size(driverRef,object,1,&a,0,NULL,&size) && size==sizeof(name));
        assert(!get(driverRef,object,1,&a,0,NULL,sizeof(name),&used,&name) && CFStringGetLength(name)>0);CFRelease(name);
    }
    a.mSelector=kAudioDevicePropertyZeroTimeStampPeriod;
    assert(!get(driverRef,KORE_DEVICE,1,&a,0,NULL,4,&used,&value) && value>=10923);
    a.mSelector=KORE_LEASE;CFBooleanRef yes=kCFBooleanTrue,no=kCFBooleanFalse,invalid=NULL;
    assert(set(driverRef,KORE_DEVICE,getpid(),&a,0,NULL,sizeof(invalid),&invalid)==kAudioHardwareBadPropertySizeError);
    assert(!set(driverRef,KORE_DEVICE,getpid(),&a,0,NULL,sizeof(yes),&yes));
    float values[8]={1,-1,.0001f,-.0001f,NAN,INFINITY,0,0};
    assert(!perform(driverRef,KORE_DEVICE,KORE_STREAM,1,kAudioServerPlugInIOOperationWriteMix,4,NULL,values,NULL));
    a.mSelector=KORE_PULL;CFDataRef data=NULL;
    assert(get(driverRef,KORE_DEVICE,getpid()+1,&a,0,NULL,sizeof(data),&used,&data)==kAudioHardwareIllegalOperationError);
    assert(!get(driverRef,KORE_DEVICE,getpid(),&a,0,NULL,sizeof(data),&used,&data) && CFDataGetLength(data)==32);
    struct kore_pcm_ring local={0};assert(kore_pcm_push(&local,(const float *)CFDataGetBytePtr(data),4)==4);CFRelease(data);
    struct kore_pcm_source source={.queue=&local,.position={0,0},.amplitude=.00025};
    unsigned char bytes[96];assert(!caiaq_pack(bytes,sizeof(bytes),96,kore_pcm_byte,&source));
    for(unsigned i=0;i<96;i++)if(i%2==0 && i%8!=4)assert(bytes[i]==0); /* inactive logical outputs */
    assert(source.consumed==4 && atomic_load(&local.underruns)>0);
    /* First stereo frame: gain .00025 and strict +/-2097 signed24 safety cap. */
    assert(bytes[1]==0 && bytes[3]==8 && bytes[7]==49);
    assert(bytes[9]==255 && bytes[11]==247 && bytes[15]==207);
    a.mSelector=KORE_LEASE;assert(!set(driverRef,KORE_DEVICE,getpid(),&a,0,NULL,sizeof(no),&no));
    assert(!start(driverRef,KORE_DEVICE,1));Float64 sample;UInt64 host,seed;
    assert(!timestamp(driverRef,KORE_DEVICE,1,&sample,&host,&seed) && host && seed && sample>=0);
    assert(!stop(driverRef,KORE_DEVICE,1));
    struct kore_pcm_ring threaded={0};pthread_t producer;assert(!pthread_create(&producer,NULL,produce,&threaded));
    for(unsigned i=0;i<100000;i++){float frame[2];while(!kore_pcm_pop(&threaded,frame,1)){} assert(frame[0]==(float)i && frame[1]==-(float)i);}
    pthread_join(producer,NULL);
    puts("PASS AudioServerPlugIn ABI: device/format, exclusive property lease, PCM IPC-to-packer, clock, 100000 threaded ring frames");
}
