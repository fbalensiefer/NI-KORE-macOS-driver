#define main capture_main
#include "../coreaudio/capture.m"
#undef main
#include <assert.h>
int main(void) {@autoreleasepool {
    float samples[4096]={0},input[512*4];for(unsigned i=0;i<2048;i++)input[i]=(float)(i%17)/32;
    struct Capture c={.samples=samples,.channels=4,.rate=44100,.capacity=1000};
    AudioBufferList list={.mNumberBuffers=1,.mBuffers={{.mNumberChannels=4,.mDataByteSize=sizeof(input),.mData=input}}};
    AudioTimeStamp ts={.mSampleTime=0,.mHostTime=100,.mFlags=kAudioTimeStampSampleTimeValid|kAudioTimeStampHostTimeValid};
    capture_io(0,NULL,&list,&ts,NULL,NULL,&c);ts.mSampleTime=512;ts.mHostTime=200;
    capture_io(0,NULL,&list,&ts,NULL,NULL,&c);
    assert(c.stored==1000 && c.observed==1024 && c.callbacks==2 && c.gaps==0 && atomic_load(&c.done));
    assert(samples[999*4+3]==input[487*4+3]);
    c=(struct Capture){.samples=samples,.channels=4,.rate=44100,.capacity=1000};ts.mSampleTime=0;
    capture_io(0,NULL,&list,&ts,NULL,NULL,&c);ts.mSampleTime=520;capture_io(0,NULL,&list,&ts,NULL,NULL,&c);
    assert(c.gaps==1 && c.maxGap==8);
    c=(struct Capture){.samples=samples,.channels=4,.rate=44100,.capacity=1000};list.mBuffers[0].mDataByteSize=3;
    capture_io(0,NULL,&list,&ts,NULL,NULL,&c);assert(c.invalid==1 && !c.stored && atomic_load(&c.done));
    puts("PASS direct HAL capture: sample accounting, final block trim, timestamp gaps, invalid buffers");
}}
