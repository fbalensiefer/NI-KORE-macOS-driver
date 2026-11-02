/* Explicit system HAL integration test: virtual output only, no USB commands. */
#import <Foundation/Foundation.h>
#include "client.h"
#include <math.h>
#include <unistd.h>
static AudioDeviceID default_device(AudioObjectPropertySelector selector) {
    AudioObjectPropertyAddress a={selector,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    AudioDeviceID device=0;UInt32 size=sizeof(device);
    return AudioObjectGetPropertyData(kAudioObjectSystemObject,&a,0,NULL,&size,&device)?0:device;
}
int main(int argc,const char **argv) {@autoreleasepool {
    if(argc!=2 || strcmp(argv[1],"--run")){puts("Dry run: --run tests the installed KORE HAL output, without opening USB");return 0;}
    AudioDeviceID before=default_device(kAudioHardwarePropertyDefaultOutputDevice);
    AudioDeviceID beforeSystem=default_device(kAudioHardwarePropertyDefaultSystemOutputDevice);
    AudioDeviceID device=kore_coreaudio_find();if(!device){fprintf(stderr,"KORE HAL device not found\n");return 1;}
    OSStatus status=kore_coreaudio_lease(device,YES);
    if(status){fprintf(stderr,"KORE HAL consumer lease status=%d\n",status);return 1;}
    NSString *directory=[[NSString stringWithUTF8String:argv[0]] stringByDeletingLastPathComponent];
    NSTask *render=[NSTask new];render.executableURL=[NSURL fileURLWithPath:[directory stringByAppendingPathComponent:@"kore-coreaudio-render"]];render.arguments=@[@"--run"];
    NSError *error=nil;
    if(![render launchAndReturnError:&error]) {fprintf(stderr,"render launch: %s\n",error.localizedDescription.UTF8String);kore_coreaudio_lease(device,NO);return 1;}
    struct kore_pcm_ring queue={0};uint64_t frames=0,nonzero=0;double peak=0;unsigned rightNonzero=0;
    for(unsigned ms=0;ms<5000;ms++) {
        status=kore_coreaudio_pull(device,&queue);if(status)break;
        float block[1024];size_t n=kore_pcm_pop(&queue,block,512);frames+=n;
        for(size_t i=0;i<n;i++) {
            if(!isfinite(block[i*2]) || !isfinite(block[i*2+1]))status=kAudioHardwareUnspecifiedError;
            if(fabs(block[i*2])>peak)peak=fabs(block[i*2]);
            if(block[i*2]!=0)nonzero++;
            if(block[i*2+1]!=0)rightNonzero++;
        }
        if(!render.running)break;
        usleep(1000);
    }
    if(render.running){[render terminate];[render waitUntilExit];status=kAudioHardwareUnspecifiedError;}
    else [render waitUntilExit];
    OSStatus released=kore_coreaudio_lease(device,NO);
    BOOL defaults=before==default_device(kAudioHardwarePropertyDefaultOutputDevice) && beforeSystem==default_device(kAudioHardwarePropertyDefaultSystemOutputDevice);
    printf("SYSTEM HAL device=%u frames=%llu nonzero_left=%llu nonzero_right=%u peak=%.6f status=%d render=%d released=%d defaults_unchanged=%d\n",device,(unsigned long long)frames,(unsigned long long)nonzero,rightNonzero,peak,status,render.terminationStatus,released,defaults);
    return status || render.terminationStatus || released || !defaults || frames<96000 || nonzero<40000 || rightNonzero || peak<.9 || peak>1.001?1:0;
}}
