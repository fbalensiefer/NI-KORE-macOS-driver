/* SPDX-License-Identifier: GPL-2.0-or-later
 * Isolated IOUSBHost transport prototype; no CoreAudio device or MIDI; tones require explicit opt-in.
 * This process owns interface 0 exclusively. Never run beside the libusb bridge.
 */
#import <Foundation/Foundation.h>
#import <IOUSBHost/IOUSBHost.h>
#include "native_plan.h"
#include "tone.h"
#include "../coreaudio/pcm_source.h"
#include "../coreaudio/client.h"
#include "native_discovery.h"
#include <signal.h>
#include <time.h>
#import <IOKit/usb/USB.h>
static volatile sig_atomic_t interrupted;
static void on_signal(int sig) {(void)sig;interrupted=1;}
static void report_error(const char *where,NSError *error) {
    fprintf(stderr,"NATIVE %s: domain=%s code=0x%08x description=%s\n",where,
            error.domain.UTF8String,(unsigned)error.code,error.localizedDescription.UTF8String);
}
static uint64_t native_now_ns(void) {
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    return (uint64_t)now.tv_sec*1000000000+(uint64_t)now.tv_nsec;
}
@interface KoreNativeSlot : NSObject {
@public
    NSMutableData *data;
    IOUSBHostIsochronousTransaction transactions[8];
    BOOL busy;
    unsigned long long sequence,frame;
}
@end
@implementation KoreNativeSlot
- (instancetype)init {if((self=[super init]))data=[NSMutableData dataWithLength:4097];return self;}
@end
@interface KoreNativeProbe : NSObject {
@public
    dispatch_queue_t queue;
    dispatch_semaphore_t done;
    IOUSBHostInterface *interface;
    IOUSBHostPipe *bulkIn,*bulkOut,*isoIn,*isoOut;
    NSMutableData *bulkData,*inData,*outData;
    IOUSBHostIsochronousTransaction inList[8],outList[8];
    int expected,profile,zeroAtStart,failed;
    unsigned guardBytes,streamDurationMs,bulkGeneration;
    BOOL streamMode,streamFinished,pcmMode;
    AudioDeviceID pcmDevice;
    struct kore_pcm_ring pcmQueue;
    struct kore_pcm_source pcmSource;
    dispatch_queue_t pcmDispatch;
    dispatch_source_t pcmTimer;
    _Atomic int pcmError;
    unsigned toneChannel;double toneAmplitude;struct caiaq_tone tone;
    NSMutableArray<KoreNativeSlot *> *streamIns,*streamOuts;
    dispatch_source_t streamTimer;
    uint64_t streamStart,streamDeadline,nextInFrame,nextOutFrame;
    unsigned long long nextRxSequence,inSubmitted,inCompletedBatches,inPackets,inZeroPackets,inBytes,inCancelled;
    unsigned long long outSubmitted,outCompletedBatches,outPackets,outBytes,outCancelled;
    unsigned long long allZeroBatches,trailingZeroBatches,maxOutBusy;
    BOOL stopping,replySeen,outCompleted;
    unsigned char reply[512];
    NSUInteger replyLength;
}
- (BOOL)open;
- (void)armBulk;
- (BOOL)exchange:(const unsigned char *)bytes length:(NSUInteger)length reply:(int)command;
- (void)startISO;
- (void)startStream;
- (BOOL)waitForStream;
- (void)reportStream;
- (BOOL)waitForCompletion;
- (BOOL)stop;
@end
@implementation KoreNativeProbe
- (instancetype)init {
    if((self=[super init])) {
        queue=dispatch_queue_create("kore.native.usb",DISPATCH_QUEUE_SERIAL);
        done=dispatch_semaphore_create(0);bulkData=[NSMutableData dataWithLength:512];
        atomic_init(&pcmQueue.written,0);atomic_init(&pcmQueue.read,0);
        atomic_init(&pcmQueue.overruns,0);atomic_init(&pcmQueue.underruns,0);atomic_init(&pcmError,0);
    }
    return self;
}
- (void)finishFailure:(const char *)where error:(NSError *)error {
    failed=1;if(error)report_error(where,error);else fprintf(stderr,"NATIVE %s failed\n",where);
    dispatch_semaphore_signal(done);
}
- (BOOL)open {
    NSError *error=nil;
    io_service_t service=caiaq_native_find_interface();
    if(!service) {fprintf(stderr,"NATIVE KORE 1 interface 0 not found or ambiguous\n");return NO;}
    if(!caiaq_native_high_speed(service)) {
        fprintf(stderr,"NATIVE high-speed link not verified; refusing timing assumptions\n");
        IOObjectRelease(service);return NO;
    }
    interface=[[IOUSBHostInterface alloc] initWithIOService:service options:IOUSBHostObjectInitOptionsNone
        queue:queue error:&error interestHandler:nil];IOObjectRelease(service);
    if(!interface) {report_error("open exclusive interface",error);return NO;}
    const IOUSBDeviceDescriptor *dd=interface.deviceDescriptor;
    if(!dd || OSSwapLittleToHostInt16(dd->idVendor)!=0x17cc || OSSwapLittleToHostInt16(dd->idProduct)!=0x4711) {
        fprintf(stderr,"NATIVE device identity mismatch\n");return NO;
    }
    if(![interface selectAlternateSetting:1 error:&error]) {report_error("alt 1",error);return NO;}
    bulkIn=[interface copyPipeWithAddress:0x81 error:&error];
    bulkOut=[interface copyPipeWithAddress:0x01 error:&error];
    isoIn=[interface copyPipeWithAddress:0x82 error:&error];
    isoOut=[interface copyPipeWithAddress:0x06 error:&error];
    if(!bulkIn || !bulkOut || !isoIn || !isoOut) {report_error("open endpoints",error);return NO;}
    for(IOUSBHostPipe *pipe in @[isoIn,isoOut]) {
        const IOUSBEndpointDescriptor *ep=&pipe.descriptors->descriptor;
        unsigned max=OSSwapLittleToHostInt16(ep->wMaxPacketSize);
        if((ep->bmAttributes&3)!=1 || ep->bInterval!=1 || max!=512) {
            fprintf(stderr,"NATIVE unexpected ISO endpoint 0x%02x max=%u interval=%u\n",ep->bEndpointAddress,max,ep->bInterval);return NO;
        }
    }
    puts("NATIVE KORE1 interface=0 alt=1, ISO endpoints validated; exclusive transport, no MIDI");
    return YES;
}
- (void)armBulk {
    if(stopping || failed)return;
    NSError *error=nil;
    unsigned generation=bulkGeneration;
    BOOL submitted=[bulkIn enqueueIORequestWithData:bulkData completionTimeout:0.5 error:&error
        completionHandler:^(IOReturn status,NSUInteger count) {
        if(self->stopping || generation!=self->bulkGeneration)return;
        if(status!=kIOReturnSuccess || count>512) {fprintf(stderr,"NATIVE bulk IN status=0x%08x actual=%lu\n",status,(unsigned long)count);[self finishFailure:"bulk IN completion" error:nil];return;}
        const unsigned char *p=self->bulkData.bytes;
        if(count && p[0]==self->expected) {
            memcpy(self->reply,p,count);self->replyLength=count;self->replySeen=YES;
            dispatch_semaphore_signal(self->done);
        } else [self armBulk];
    }];
    if(!submitted)[self finishFailure:"bulk IN submit" error:error];
}
- (BOOL)waitForCompletion {
    /* Main thread only; callbacks run on queue. Cancellation is bounded by abort. */
    for(unsigned tries=0;tries<20 && !interrupted;tries++)
        if(!dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,50*NSEC_PER_MSEC)))return YES;
    fprintf(stderr,"NATIVE request timeout or signal\n");return NO;
}
- (BOOL)exchange:(const unsigned char *)bytes length:(NSUInteger)length reply:(int)command {
    unsigned attempts=command==1?4:1;
    for(unsigned attempt=0;attempt<attempts && !interrupted;attempt++) {
        if(attempt) {
            dispatch_sync(queue,^{self->stopping=YES;self->bulkGeneration++;});
            NSError *error=nil;
            if(![bulkIn abortWithOption:IOUSBHostAbortOptionSynchronous error:&error]) {
                report_error("bulk retry abort",error);return NO;
            }
            dispatch_sync(queue,^{self->stopping=NO;self->failed=0;});
            while(!dispatch_semaphore_wait(done,DISPATCH_TIME_NOW)) {}
            printf("NATIVE GET_DEVICE_INFO retry=%u/3\n",attempt);
        }
        dispatch_sync(queue,^{
            self->bulkGeneration++;self->expected=command;self->replySeen=NO;self->replyLength=0;
            [self armBulk];
            if(self->failed)return;
            NSError *error=nil;NSUInteger sent=0;
            NSMutableData *data=[NSMutableData dataWithBytes:bytes length:length];
            if(![self->bulkOut sendIORequestWithData:data bytesTransferred:&sent completionTimeout:0.5 error:&error] || sent!=length)
                [self finishFailure:"bulk OUT command" error:error];
        });
        BOOL completed=[self waitForCompletion];
        __block BOOL success=NO;
        dispatch_sync(queue,^{success=self->replySeen && !self->failed;});
        if(completed && success)return YES;
    }
    return NO;
}
- (void)startISO {
    inData=[NSMutableData dataWithLength:4096];
    for(unsigned i=0;i<8;i++) {inList[i].requestCount=512;inList[i].offset=i*512;}
    NSError *error=nil;
    BOOL submitted=[isoIn enqueueIORequestWithData:inData transactionList:inList transactionListCount:8
        firstFrameNumber:0 options:IOUSBHostIsochronousTransferOptionsNone error:&error
        completionHandler:^(IOReturn status,IOUSBHostIsochronousTransaction *transactions) {
        if(self->stopping)return;
        printf("NATIVE ISO IN completion status=0x%08x\n",status);
        if(status!=kIOReturnSuccess && status!=kIOReturnUnderrun) {[self finishFailure:"ISO IN completion" error:nil];return;}
        unsigned lengths[8];
        for(unsigned i=0;i<8;i++) {
            IOReturn packetStatus=transactions[i].status;
            if((packetStatus!=kIOReturnSuccess && packetStatus!=kIOReturnUnderrun) || transactions[i].completeCount>512) {
                [self finishFailure:"ISO IN packet" error:nil];return;
            }
            lengths[i]=transactions[i].completeCount;
        }
        printf("NATIVE ISO IN lengths=");for(unsigned i=0;i<8;i++)printf("%s%u",i?",":"",lengths[i]);puts("");
        if(self->profile>=0)caiaq_probe_layout((unsigned)self->profile,lengths);
        struct caiaq_native_plan plan;
        if(caiaq_native_plan(lengths,self->zeroAtStart,self->guardBytes,&plan)) {[self finishFailure:"native layout" error:nil];return;}
        self->outData=[NSMutableData dataWithLength:plan.backing_bytes];
        printf("NATIVE OUT submit payload=%zu backing=%zu zero-offset=%s lengths=",plan.payload_bytes,plan.backing_bytes,self->zeroAtStart?"start":"compact");
        for(unsigned i=0;i<8;i++) {
            caiaq_pack((unsigned char *)self->outData.mutableBytes+plan.payload_offsets[i],
                plan.backing_bytes-plan.payload_offsets[i],lengths[i],NULL,NULL);
            self->outList[i].requestCount=lengths[i];self->outList[i].offset=plan.transaction_offsets[i];
            printf("%s%u",i?",":"",lengths[i]);
        }
        printf(" offsets=");for(unsigned i=0;i<8;i++)printf("%s%u",i?",":"",self->outList[i].offset);
        puts("; all 8 time slots preserved, one silence batch");
        NSError *outError=nil;
        BOOL sent=[self->isoOut enqueueIORequestWithData:self->outData transactionList:self->outList transactionListCount:8
            firstFrameNumber:0 options:IOUSBHostIsochronousTransferOptionsNone error:&outError
            completionHandler:^(IOReturn outStatus,IOUSBHostIsochronousTransaction *outTransactions) {
            if(self->stopping)return;
            BOOL valid=outStatus==kIOReturnSuccess;
            unsigned total=0;
            for(unsigned i=0;i<8;i++) {
                printf("NATIVE OUT packet=%u status=0x%08x requested=%u actual=%u\n",i,
                    outTransactions[i].status,outTransactions[i].requestCount,outTransactions[i].completeCount);
                if(outTransactions[i].status!=kIOReturnSuccess || outTransactions[i].completeCount!=outTransactions[i].requestCount)valid=NO;
                total+=outTransactions[i].completeCount;
            }
            self->outCompleted=valid;
            if(!valid)self->failed=1;
            printf("NATIVE OUT result status=0x%08x completed=%d bytes=%u\n",outStatus,valid,total);
            dispatch_semaphore_signal(self->done);
        }];
        if(!sent)[self finishFailure:"ISO OUT native submit" error:outError];
    }];
    if(!submitted)[self finishFailure:"ISO IN submit" error:error];
}
- (BOOL)streamCanContinue {
    if(stopping || failed)return NO;
    if(atomic_load(&pcmError)) {[self finishFailure:"CoreAudio property IPC" error:nil];return NO;}
    if(native_now_ns()>=streamDeadline) {
        stopping=YES;streamFinished=YES;dispatch_semaphore_signal(done);return NO;
    }
    return YES;
}
- (void)submitStreamOut:(const unsigned *)lengths {
    KoreNativeSlot *slot=nil;
    for(KoreNativeSlot *candidate in streamOuts)if(!candidate->busy){slot=candidate;break;}
    if(!slot) {[self finishFailure:"OUT pool exhausted (no packets dropped)" error:nil];return;}
    struct caiaq_native_plan plan;
    unsigned any=0;for(unsigned i=0;i<8;i++)any|=lengths[i];
    if(!any) { /* Retain all eight empty time slots; backing storage still exists. */
        memset(&plan,0,sizeof(plan));plan.backing_bytes=1;
    } else if(caiaq_native_plan(lengths,1,1,&plan)) {[self finishFailure:"stream OUT layout" error:nil];return;}
    memset(slot->transactions,0,sizeof(slot->transactions));
    for(unsigned i=0;i<8;i++) {
        if(caiaq_pack((unsigned char *)slot->data.mutableBytes+plan.payload_offsets[i],
            slot->data.length-plan.payload_offsets[i],lengths[i],toneChannel?caiaq_tone_byte:pcmMode?kore_pcm_byte:NULL,toneChannel?(void *)&tone:pcmMode?(void *)&pcmSource:NULL)) {
            [self finishFailure:"stream silence packing" error:nil];return;
        }
        slot->transactions[i].requestCount=lengths[i];
        slot->transactions[i].offset=plan.transaction_offsets[i];
    }
    uint64_t now=[interface frameNumberWithTime:NULL];
    if(!now) {[self finishFailure:"OUT frame clock" error:nil];return;}
    if(!nextOutFrame)nextOutFrame=now+32;
    if(nextOutFrame<now+4) {[self finishFailure:"OUT scheduling deadline missed" error:nil];return;}
    slot->frame=nextOutFrame++;slot->busy=YES;outSubmitted++;
    unsigned busy=0;for(KoreNativeSlot *candidate in streamOuts)if(candidate->busy)busy++;
    if(busy>maxOutBusy)maxOutBusy=busy;
    NSError *error=nil;
    BOOL submitted=[isoOut enqueueIORequestWithData:slot->data transactionList:slot->transactions transactionListCount:8
        firstFrameNumber:slot->frame options:IOUSBHostIsochronousTransferOptionsNone error:&error
        completionHandler:^(IOReturn status,IOUSBHostIsochronousTransaction *list) {
        slot->busy=NO;
        if(self->stopping || self->failed) {self->outCancelled++;dispatch_semaphore_signal(self->done);return;}
        if(status!=kIOReturnSuccess) {
            fprintf(stderr,"NATIVE stream OUT status=0x%08x frame=%llu\n",status,slot->frame);
            [self finishFailure:"stream OUT completion" error:nil];return;
        }
        for(unsigned i=0;i<8;i++) {
            if(list[i].status!=kIOReturnSuccess || list[i].completeCount!=list[i].requestCount) {
                fprintf(stderr,"NATIVE stream OUT packet=%u status=0x%08x requested=%u actual=%u\n",i,list[i].status,list[i].requestCount,list[i].completeCount);
                [self finishFailure:"stream OUT packet" error:nil];return;
            }
            self->outPackets++;self->outBytes+=list[i].completeCount;
        }
        self->outCompletedBatches++;
    }];
    if(!submitted) {slot->busy=NO;fprintf(stderr,"NATIVE OUT schedule: requested=%llu current=%llu lead=%lld frames\n",slot->frame,[interface frameNumberWithTime:NULL],(long long)slot->frame-(long long)[interface frameNumberWithTime:NULL]);[self finishFailure:"stream OUT submit" error:error];}
}
- (void)submitStreamIn:(KoreNativeSlot *)slot {
    if(![self streamCanContinue])return;
    uint64_t now=[interface frameNumberWithTime:NULL];
    if(!now || nextInFrame<=now) {[self finishFailure:"IN scheduling deadline missed" error:nil];return;}
    memset(slot->transactions,0,sizeof(slot->transactions));
    for(unsigned i=0;i<8;i++) {slot->transactions[i].requestCount=512;slot->transactions[i].offset=i*512;}
    slot->sequence=inSubmitted++;slot->frame=nextInFrame++;slot->busy=YES;
    NSError *error=nil;
    BOOL submitted=[isoIn enqueueIORequestWithData:slot->data transactionList:slot->transactions transactionListCount:8
        firstFrameNumber:slot->frame options:IOUSBHostIsochronousTransferOptionsNone error:&error
        completionHandler:^(IOReturn status,IOUSBHostIsochronousTransaction *list) {
        slot->busy=NO;
        if(self->stopping || self->failed) {self->inCancelled++;dispatch_semaphore_signal(self->done);return;}
        if(![self streamCanContinue]) {self->inCancelled++;dispatch_semaphore_signal(self->done);return;}
        if(status!=kIOReturnSuccess && status!=kIOReturnUnderrun) {
            fprintf(stderr,"NATIVE stream IN status=0x%08x frame=%llu\n",status,slot->frame);
            [self finishFailure:"stream IN completion" error:nil];return;
        }
        if(slot->sequence!=self->nextRxSequence++) {[self finishFailure:"IN completion order" error:nil];return;}
        unsigned lengths[8];
        for(unsigned i=0;i<8;i++) {
            if((list[i].status!=kIOReturnSuccess && list[i].status!=kIOReturnUnderrun) || list[i].completeCount>512) {
                fprintf(stderr,"NATIVE stream IN packet=%u status=0x%08x actual=%u\n",i,list[i].status,list[i].completeCount);
                [self finishFailure:"stream IN packet" error:nil];return;
            }
            lengths[i]=list[i].completeCount;self->inPackets++;self->inBytes+=lengths[i];
            if(!lengths[i])self->inZeroPackets++;
        }
        self->inCompletedBatches++;
        unsigned any=0;for(unsigned i=0;i<8;i++)any|=lengths[i];
        if(!any)self->allZeroBatches++;
        if(!lengths[7])self->trailingZeroBatches++;
        [self submitStreamOut:lengths];
        if(self->failed)return;
        [self submitStreamIn:slot];
    }];
    if(!submitted) {slot->busy=NO;[self finishFailure:"stream IN submit" error:error];}
}
- (void)startStream {
    streamIns=[NSMutableArray array];streamOuts=[NSMutableArray array];
    for(unsigned i=0;i<32;i++)[streamIns addObject:[KoreNativeSlot new]];
    for(unsigned i=0;i<64;i++)[streamOuts addObject:[KoreNativeSlot new]];
    uint64_t now=[interface frameNumberWithTime:NULL];
    if(!now) {[self finishFailure:"stream frame clock" error:nil];return;}
    if(toneChannel && caiaq_tone_init(&tone,toneChannel,toneAmplitude)) {[self finishFailure:"tone configuration" error:nil];return;}
    if(pcmMode) {
        pcmSource=(struct kore_pcm_source){.queue=&pcmQueue,.position={4,4},.amplitude=0.00025};
        pcmDispatch=dispatch_queue_create("kore.coreaudio.prefetch",DISPATCH_QUEUE_SERIAL);
        pcmTimer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,pcmDispatch);
        dispatch_source_set_timer(pcmTimer,dispatch_time(DISPATCH_TIME_NOW,0),NSEC_PER_MSEC,0);
        dispatch_source_set_event_handler(pcmTimer,^{
            OSStatus status=kore_coreaudio_pull(self->pcmDevice,&self->pcmQueue);
            if(status)atomic_store(&self->pcmError,status);
        });dispatch_resume(pcmTimer);
    }
    streamStart=native_now_ns();streamDeadline=streamStart+(uint64_t)streamDurationMs*1000000;
    nextInFrame=now+4;
    streamTimer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,queue);
    dispatch_source_set_timer(streamTimer,dispatch_time(DISPATCH_TIME_NOW,(int64_t)streamDurationMs*NSEC_PER_MSEC),DISPATCH_TIME_FOREVER,0);
    dispatch_source_set_event_handler(streamTimer,^{
        self->stopping=YES;self->streamFinished=YES;dispatch_semaphore_signal(self->done);
    });
    dispatch_resume(streamTimer);
    printf("NATIVE STREAM %s: 32 IN / 64 OUT slots, OUT lead=32ms, 8 microframes each, explicit contiguous frame numbers\n",toneChannel?"OPT-IN SINGLE-CHANNEL TONE":pcmMode?"EXPLICIT COREAUDIO PCM (capped .00025)":"silence");
    for(KoreNativeSlot *slot in streamIns) {if(failed || stopping)break;[self submitStreamIn:slot];}
}
- (BOOL)waitForStream {
    for(unsigned elapsed=0;elapsed<streamDurationMs+500 && !interrupted;elapsed+=50)
        if(!dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,50*NSEC_PER_MSEC)))return YES;
    fprintf(stderr,"NATIVE stream wait timeout or signal\n");return NO;
}
- (void)reportStream {
    printf("NATIVE STREAM final duration_ms=%.1f failed=%d deadline_reached=%d IN submitted=%llu completed=%llu packets=%llu zero=%llu bytes=%llu cancelled=%llu OUT submitted=%llu completed=%llu packets=%llu bytes=%llu cancelled=%llu\n",
        streamStart?(native_now_ns()-streamStart)/1000000.0:0.0,failed,streamFinished,
        inSubmitted,inCompletedBatches,inPackets,inZeroPackets,inBytes,inCancelled,
        outSubmitted,outCompletedBatches,outPackets,outBytes,outCancelled);
    if(pcmMode)printf("NATIVE COREAUDIO consumed_frames=%llu underflows=%llu overruns=%llu\n",(unsigned long long)pcmSource.consumed,(unsigned long long)atomic_load(&pcmQueue.underruns),(unsigned long long)atomic_load(&pcmQueue.overruns));
    if(toneChannel)printf("NATIVE TONE channel=%u amplitude=%.8f frequency=440 PCM_rate=48000 generated_samples=%llu nonzero_samples=%llu\n",toneChannel,toneAmplitude,(unsigned long long)tone.generated,(unsigned long long)tone.nonzero);
    printf("NATIVE STREAM layouts all_zero=%llu trailing_zero=%llu peak_OUT_busy=%llu/64\n",allZeroBatches,trailingZeroBatches,maxOutBusy);
}
- (BOOL)stop {
    dispatch_sync(queue,^{self->stopping=YES;if(self->streamTimer)dispatch_source_cancel(self->streamTimer);});
    if(pcmTimer) {dispatch_source_cancel(pcmTimer);dispatch_sync(pcmDispatch,^{});pcmTimer=nil;}
    if(pcmDevice)kore_coreaudio_lease(pcmDevice,NO);
    BOOL success=YES;
    for(IOUSBHostPipe *pipe in @[bulkIn?:[NSNull null],bulkOut?:[NSNull null],isoIn?:[NSNull null],isoOut?:[NSNull null]]) {
        if((id)pipe==[NSNull null])continue;
        NSError *error=nil;
        if(![pipe abortWithOption:IOUSBHostAbortOptionSynchronous error:&error]) {
            report_error("synchronous abort",error);success=NO;
        }
    }
    if(!success)return NO; /* caller exits without releasing active backing storage */
    /* Native abort can finish kernel IO before its callback is enqueued. Keep
     * every buffer alive while draining late callbacks on the serial queue. */
    uint64_t drainDeadline=native_now_ns()+500000000;
    __block unsigned pending=0;
    do {
        dispatch_sync(queue,^{
            pending=0;
            for(KoreNativeSlot *slot in self->streamIns)if(slot->busy)pending++;
            for(KoreNativeSlot *slot in self->streamOuts)if(slot->busy)pending++;
        });
        if(!pending)break;
        dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_MSEC));
    } while(native_now_ns()<drainDeadline);
    if(pending) {fprintf(stderr,"NATIVE callback drain timeout pending=%u; retaining buffers\n",pending);return NO;}
    [interface destroy];dispatch_sync(queue,^{});
    if(streamMode)[self reportStream];
    streamTimer=nil;
    puts("NATIVE transport closed");return YES;
}
@end
int main(int argc,const char **argv) {
    @autoreleasepool {
        int profile=caiaq_probe_id("observed"),zeroAtStart=1;unsigned guard=1;BOOL run=NO,discover=NO,stream=NO,pcmMode=NO,layoutSet=NO,silenceSet=NO,amplitudeSet=NO;unsigned duration=3000,toneChannel=0;double toneAmplitude=0.00025;
        for(int i=1;i<argc;i++) {
            if(!strcmp(argv[i],"--coreaudio-pcm"))pcmMode=stream=YES;
            else if(!strcmp(argv[i],"--stream-silence"))stream=silenceSet=YES;
            else if(!strcmp(argv[i],"--stream-tone") && i+1<argc) {
                char *end;unsigned long n=strtoul(argv[++i],&end,10);
                if(!*argv[i] || *end || n<1 || n>4 || toneChannel)return 2;
                toneChannel=(unsigned)n;stream=YES;
            } else if(!strcmp(argv[i],"--tone-amplitude") && i+1<argc) {
                amplitudeSet=YES;char *end;toneAmplitude=strtod(argv[++i],&end);
                if(!*argv[i] || *end || !isfinite(toneAmplitude) || toneAmplitude<0 || toneAmplitude>CAIAQ_MAX_AMPLITUDE)return 2;
            }
            else if(!strcmp(argv[i],"--duration-ms") && i+1<argc) {
                char *end;unsigned long n=strtoul(argv[++i],&end,10);
                if(!*argv[i] || *end || n<100 || n>3000)return 2;duration=(unsigned)n;
            }
            else if(!strcmp(argv[i],"--discover"))discover=YES;
            else if(!strcmp(argv[i],"--run"))run=YES;
            else if(!strcmp(argv[i],"--dry-run"))run=NO;
            else if(!strcmp(argv[i],"--layout") && i+1<argc) {
                layoutSet=YES;const char *name=argv[++i];profile=!strcmp(name,"rx")?-1:caiaq_probe_id(name);
                if(profile<0 && strcmp(name,"rx"))return 2;
            } else if(!strcmp(argv[i],"--zero-offset") && i+1<argc) {
                const char *name=argv[++i];if(strcmp(name,"start") && strcmp(name,"compact"))return 2;
                zeroAtStart=!strcmp(name,"start");
            } else if(!strcmp(argv[i],"--buffer-guard") && i+1<argc) {
                const char *n=argv[++i];if(strcmp(n,"0") && strcmp(n,"1"))return 2;guard=(unsigned)(n[0]-'0');
            } else {fprintf(stderr,"Usage: %s [--run|--dry-run|--discover] [--coreaudio-pcm | --stream-silence --duration-ms 100..3000 | --stream-tone 1..4 --tone-amplitude 0..0.001] [--layout observed|rx|PROFILE] [--zero-offset start|compact] [--buffer-guard 0|1]\n",argv[0]);return 2;}
        }
        if(pcmMode && (toneChannel || silenceSet)) {fprintf(stderr,"CoreAudio PCM excludes silence/tone modes\n");return 2;}
        if(amplitudeSet && !toneChannel) {fprintf(stderr,"tone-amplitude requires stream-tone\n");return 2;}
        if(toneChannel && silenceSet) {fprintf(stderr,"silence and tone are mutually exclusive\n");return 2;}
        if(toneChannel && duration!=3000) {fprintf(stderr,"tone test requires fixed 3000ms session\n");return 2;}
        if(stream && (layoutSet || !zeroAtStart || guard!=1 || discover)) {
            fprintf(stderr,"stream-silence requires RX lengths, zero-offset=start, buffer-guard=1; excludes discover/layout\n");return 2;
        }
        setvbuf(stdout,NULL,_IOLBF,0);
        printf("NATIVE CONFIG hardware=%s layout=%s zero-offset=%s backing-guard=%u (not transmitted), PCM=%s, mode=%s\n",
            run?"ENABLED":"disabled",(profile<0 || stream)?"rx":caiaq_probe_names[profile],zeroAtStart?"start":"compact",guard,toneChannel?"EXPLICIT TONE":pcmMode?"EXPLICIT COREAUDIO PCM":"silence",stream?"bounded RX stream":"one batch");
        if(toneChannel)printf("NATIVE CONFIG tone_channel=%u amplitude=%.8f (ceiling=0.001), 0.5s silence / 1s 440Hz / silence\n",toneChannel,toneAmplitude);
        if(stream)printf("NATIVE CONFIG stream=%s duration_ms=%u (hard maximum 3000)\n",toneChannel?"tone":pcmMode?"coreaudio-pcm":"silence",duration);
        if(discover) {
            if(run) {fprintf(stderr,"--discover excludes --run\n");return 2;}
            io_service_t service=caiaq_native_find_interface();
            if(!service)return 1;
            BOOL highSpeed=caiaq_native_high_speed(service);IOObjectRelease(service);
            puts("NATIVE discovery only: no user client opened, no USB commands sent");
            return highSpeed?0:1;
        }
        if(!run)return 0;
        signal(SIGINT,on_signal);signal(SIGTERM,on_signal);
        KoreNativeProbe *probe=[KoreNativeProbe new];probe->profile=profile;probe->zeroAtStart=zeroAtStart;probe->guardBytes=guard;probe->streamMode=stream;probe->streamDurationMs=duration;probe->toneChannel=toneChannel;probe->toneAmplitude=toneAmplitude;probe->pcmMode=pcmMode;
        int rc=1;
        if(pcmMode) {
            probe->pcmDevice=kore_coreaudio_find();
            if(!probe->pcmDevice || kore_coreaudio_lease(probe->pcmDevice,YES)) {
                fprintf(stderr,"CoreAudio prototype unavailable or already leased; no USB opened\n");return 1;
            }
        }
        if([probe open]) {
            const unsigned char info[]={1},params[]={9,1,2,176,0,1};
            if([probe exchange:info length:sizeof(info) reply:1]) {
                const unsigned char *p=probe->reply+1;
                if(probe->replyLength>=15 && p[7]==4 && p[8]==2 && p[9]==0 && p[10]==0 && p[13]==2) {
                    if([probe exchange:params length:sizeof(params) reply:9] && probe->replyLength>=2 && probe->reply[1]==1) {
                        puts("NATIVE AUDIO_PARAMS accepted");
                        if(stream) {
                            dispatch_sync(probe->queue,^{[probe startStream];});
                            if([probe waitForStream]) {
                                dispatch_sync(probe->queue,^{});
                                if(probe->streamFinished && !probe->failed && probe->outCompletedBatches && (!pcmMode || probe->pcmSource.consumed))rc=0;
                            }
                        } else {
                            dispatch_sync(probe->queue,^{[probe startISO];});
                            if([probe waitForCompletion]) {
                                dispatch_sync(probe->queue,^{});
                                if(probe->outCompleted && !probe->failed)rc=0;
                            }
                        }
                    } else fprintf(stderr,"NATIVE AUDIO_PARAMS not accepted; no ISO started\n");
                } else fprintf(stderr,"NATIVE unexpected device specification; no AUDIO_PARAMS sent\n");
            }
        }
        if(![probe stop]) {fflush(NULL);_Exit(1);}
        return rc;
    }
}
