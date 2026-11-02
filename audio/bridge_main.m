/* SPDX-License-Identifier: GPL-2.0-or-later
 * Shared native USB owner: CoreMIDI reports + host-paced CoreAudio output.
 */
#define KORE_NATIVE_NO_MAIN
#include "native_probe.m"
#include "system_install.h"
static void bridge_usage(const char *name) {
    printf("Usage: %s [--run|--dry-run] [--midi-only|--coreaudio-pcm|--stream-silence|--stream-tone 1..4] [--mixxx-feedback|--no-midi] [--pcm-gain 0..0.001] [--duration-ms 100..3000|--continuous]\n",name);
    puts("Default: dry run, MIDI + silence. Default PCM gain is .00025; explicit ceiling .001 on logical outputs 3/4.");
    puts("--continuous explicitly enables ongoing silence/CoreAudio operation; Ctrl+C stops.");
    puts("--system-status | --install-system | --uninstall-system | --install-login | --uninstall-login | --emit-driver-plist");
}
int main(int argc,const char **argv) {@autoreleasepool {
    if(argc==2) {
        if(!strcmp(argv[1],"--system-status"))return kore_system_status();
        if(!strcmp(argv[1],"--install-system"))return kore_system_install(NO);
        if(!strcmp(argv[1],"--uninstall-system"))return kore_system_install(YES);
        if(!strcmp(argv[1],"--install-login"))return kore_login_install(NO);
        if(!strcmp(argv[1],"--uninstall-login"))return kore_login_install(YES);
        if(!strcmp(argv[1],"--emit-driver-plist")){fputs(KORE_INFO_PLIST,stdout);return 0;}
    }
    BOOL run=NO,midi=YES,feedback=NO,pcm=NO,midiOnly=NO,continuous=NO,modeSet=NO,durationSet=NO,gainSet=NO;double pcmGain=.00025;
    unsigned duration=3000,toneChannel=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--help")){bridge_usage(argv[0]);return 0;}
        else if(!strcmp(argv[i],"--run"))run=YES;
        else if(!strcmp(argv[i],"--dry-run"))run=NO;
        else if(!strcmp(argv[i],"--mixxx-feedback")){midi=YES;feedback=YES;}
        else if(!strcmp(argv[i],"--no-midi")){midi=NO;feedback=NO;}
        else if(!strcmp(argv[i],"--pcm-gain") && i+1<argc) {
            char *end;pcmGain=strtod(argv[++i],&end);gainSet=YES;
            if(!*argv[i] || *end || !isfinite(pcmGain) || pcmGain<0 || pcmGain>CAIAQ_MAX_AMPLITUDE)return 2;
        }
        else if(!strcmp(argv[i],"--continuous"))continuous=YES;
        else if(!strcmp(argv[i],"--midi-only")){if(modeSet)return 2;modeSet=midiOnly=YES;}
        else if(!strcmp(argv[i],"--coreaudio-pcm")){if(modeSet)return 2;modeSet=pcm=YES;}
        else if(!strcmp(argv[i],"--stream-silence")){if(modeSet)return 2;modeSet=YES;}
        else if(!strcmp(argv[i],"--stream-tone") && i+1<argc) {
            if(modeSet)return 2;modeSet=YES;char *end;unsigned long n=strtoul(argv[++i],&end,10);
            if(!*argv[i] || *end || n<1 || n>4)return 2;toneChannel=(unsigned)n;
        } else if(!strcmp(argv[i],"--duration-ms") && i+1<argc) {
            char *end;unsigned long n=strtoul(argv[++i],&end,10);
            if(!*argv[i] || *end || n<100 || n>3000)return 2;duration=(unsigned)n;durationSet=YES;
        } else {bridge_usage(argv[0]);return 2;}
    }
    if((gainSet && !pcm) || (continuous && durationSet) || (midiOnly && !midi) || (toneChannel && (continuous || duration!=3000)))return 2;
    setvbuf(stdout,NULL,_IOLBF,0);
    printf("BRIDGE CONFIG hardware=%s MIDI=%d feedback=%d audio=%s continuous=%d duration_ms=%u\n",run?"ENABLED":"disabled",midi,feedback,midiOnly?"off":pcm?"CoreAudio PCM":toneChannel?"capped tone":"silence",continuous,duration);
    if(pcm)printf("BRIDGE PCM gain=%.8f (ceiling=.001), stereo logical outputs 3/4\n",pcmGain);
    if(!run)return 0;
    interrupted=0;signal(SIGINT,on_signal);signal(SIGTERM,on_signal);
    KoreNativeProbe *probe=[KoreNativeProbe new];
    probe->profile=caiaq_probe_id("observed");probe->zeroAtStart=1;probe->guardBytes=1;
    probe->streamMode=!midiOnly;probe->streamDurationMs=duration;probe->continuous=continuous;
    probe->toneChannel=toneChannel;probe->toneAmplitude=.00025;probe->pcmMode=pcm;probe->pcmAmplitude=pcmGain;probe->feedbackMode=feedback;
    int rc=1;
    if(pcm) {
        probe->pcmDevice=kore_coreaudio_find();
        if(!probe->pcmDevice || kore_coreaudio_lease(probe->pcmDevice,YES)) {
            fprintf(stderr,"CoreAudio device unavailable or already leased; no USB opened\n");return 1;
        }
    }
    if([probe open]) {
        const unsigned char info[]={1},params[]={9,1,2,176,0,1};
        if([probe exchange:info length:sizeof(info) reply:1]) {
            const unsigned char *p=probe->reply+1;
            BOOL valid=probe->replyLength>=15 && p[7]==4 && p[8]==2 && p[9]==0 && p[10]==0 && p[13]==2;
            if(!valid)fprintf(stderr,"Unexpected KORE specification; refusing stream\n");
            else if(midiOnly || ([probe exchange:params length:sizeof(params) reply:9] && probe->replyLength>=2 && probe->reply[1]==1)) {
                if(!midi || [probe startMIDI]) {
                    if(midiOnly) {
                        uint64_t until=native_now_ns()+(uint64_t)duration*1000000;
                        while(!interrupted && (continuous || native_now_ns()<until)) {
                            __block BOOL failed;dispatch_sync(probe->queue,^{failed=probe->failed;});
                            if(failed || atomic_load(&probe->controllerError))break;
                            usleep(10000);
                        }
                        __block BOOL failed;dispatch_sync(probe->queue,^{failed=probe->failed;});
                        rc=failed || atomic_load(&probe->controllerError)?1:0;
                    } else {
                        dispatch_sync(probe->queue,^{[probe startStream];});
                        if([probe waitForStream]) {
                            __block BOOL success;dispatch_sync(probe->queue,^{success=!probe->failed && probe->outCompletedBatches && (probe->streamFinished || (continuous && interrupted));});
                            if(success)rc=0;
                        }
                    }
                }
            }
        }
    }
    if(![probe stop]){fflush(NULL);_Exit(1);}
    return rc;
}}
