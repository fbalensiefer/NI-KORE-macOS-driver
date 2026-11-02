/* SPDX-License-Identifier: GPL-2.0-or-later
 * Explicit test of the virtual KORE device only, never the default output.
 * Full-scale virtual samples become .00025 peak at the bounded USB bridge.
 */
#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#include <stdatomic.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include "client.h"
struct Render {uint64_t frames;_Atomic int done,failed;};
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
        uint64_t index=r->frames+n;float value=0;
        if(index>=24000 && index<72000) {
            uint64_t k=index-24000;double gain=1;if(k<240)gain=k/240.;if(k>=47760)gain=(47999-k)/240.;
            value=(float)(gain*sin(6.283185307179586*440*(k%1200)/48000.));
        }
        unsigned channel=0;
        for(UInt32 b=0;b<out->mNumberBuffers;b++) {
            float *values=out->mBuffers[b].mData;
            for(unsigned ch=0;ch<out->mBuffers[b].mNumberChannels;ch++,channel++)
                values[n*out->mBuffers[b].mNumberChannels+ch]=channel==0?value:0;
        }
    }
    r->frames+=frames;if(r->frames>=120000)atomic_store(&r->done,1);return 0;
}
int main(int argc,const char **argv) {@autoreleasepool {
    if(argc!=2 || strcmp(argv[1],"--run")){puts("Dry run: --run sends a test signal exclusively to the KORE experimental virtual device");return 0;}
    AudioDeviceID device=kore_coreaudio_find();if(!device){fprintf(stderr,"KORE virtual output not installed\n");return 1;}
    AudioStreamBasicDescription format={0};UInt32 size=sizeof(format);
    AudioObjectPropertyAddress a={kAudioDevicePropertyStreamFormat,kAudioObjectPropertyScopeOutput,kAudioObjectPropertyElementMain};
    if(AudioObjectGetPropertyData(device,&a,0,NULL,&size,&format) || format.mSampleRate!=48000 || format.mChannelsPerFrame!=2 || format.mBitsPerChannel!=32 || !(format.mFormatFlags&kAudioFormatFlagIsFloat))return 1;
    struct Render state={0};AudioDeviceIOProcID proc=NULL;OSStatus status=AudioDeviceCreateIOProcID(device,render,&state,&proc);
    if(status)return 1;status=AudioDeviceStart(device,proc);
    for(unsigned i=0;!status && !atomic_load(&state.done) && i<350;i++)usleep(10000);
    OSStatus stopped=AudioDeviceStop(device,proc),destroyed=AudioDeviceDestroyIOProcID(device,proc);
    if(stopped || destroyed){fprintf(stderr,"Render teardown failed %d/%d\n",stopped,destroyed);fflush(NULL);_Exit(1);}
    printf("Virtual output render frames=%llu status=%d\n",(unsigned long long)state.frames,status);
    return status || atomic_load(&state.failed) || !atomic_load(&state.done)?1:0;
}}
