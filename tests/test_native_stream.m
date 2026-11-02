/* Production stream callbacks, deferred mock completions, no USB service access. */
#define main native_probe_main
#include "../audio/native_probe.m"
#undef main
#include <assert.h>
@interface MockFrameClock : NSObject { @public uint64_t now;unsigned delay; }
@end
@implementation MockFrameClock
- (uint64_t)frameNumberWithTime:(IOUSBHostTime *)time {(void)time;return now;}
- (void)destroy {}
@end
@interface MockRequest : NSObject { @public
    NSMutableData *data;
    IOUSBHostIsochronousTransaction *list;
    IOUSBHostIsochronousTransactionCompletionHandler callback;
    uint64_t frame;
}
@end
@implementation MockRequest
@end
@interface MockStreamPipe : NSObject { @public
    NSMutableArray<MockRequest *> *pending;
    uint64_t lastFrame;
    dispatch_queue_t deferredAbortQueue;
    BOOL reject,input;
    unsigned submissions;
}
@end
@implementation MockStreamPipe
- (instancetype)init {if((self=[super init]))pending=[NSMutableArray array];return self;}
- (BOOL)enqueueIORequestWithData:(NSMutableData *)data transactionList:(IOUSBHostIsochronousTransaction *)list
          transactionListCount:(NSUInteger)count firstFrameNumber:(uint64_t)first
                       options:(IOUSBHostIsochronousTransferOptions)options error:(NSError **)error
             completionHandler:(IOUSBHostIsochronousTransactionCompletionHandler)callback {
    assert(count==8 && options==0 && data.length==4097 && first>0);
    assert(!lastFrame || first==lastFrame+1);lastFrame=first;
    if(!input)for(unsigned i=0;i<8;i++) {
        unsigned expected=(submissions%7 && (i%2))?176:0;
        assert(list[i].requestCount==expected); /* RX order and empty slots survive unchanged */
    }
    submissions++;
    for(MockRequest *request in pending)assert(request->list!=list); /* no live buffer reuse */
    if(reject) {*error=[NSError errorWithDomain:IOUSBHostErrorDomain code:kIOReturnBadArgument userInfo:nil];return NO;}
    MockRequest *request=[MockRequest new];request->data=data;request->list=list;request->callback=callback;request->frame=first;
    [pending addObject:request];assert(pending.count<=256);return YES;
}
- (BOOL)abortWithOption:(IOUSBHostAbortOption)option error:(NSError **)error {
    (void)error;assert(option==IOUSBHostAbortOptionSynchronous);
    while(pending.count) {
        MockRequest *request=pending[0];[pending removeObjectAtIndex:0];
        if(deferredAbortQueue)dispatch_after(dispatch_time(DISPATCH_TIME_NOW,30*NSEC_PER_MSEC),deferredAbortQueue,^{request->callback(kIOReturnAborted,request->list);});
        else request->callback(kIOReturnAborted,request->list);
    }
    return YES;
}
@end
static void completeOut(MockStreamPipe *out,uint64_t now) {
    while(out->pending.count && out->pending[0]->frame<=now) {
        MockRequest *request=out->pending[0];[out->pending removeObjectAtIndex:0];
        for(unsigned i=0;i<8;i++) {
            request->list[i].status=kIOReturnSuccess;request->list[i].completeCount=request->list[i].requestCount;
            if(!request->list[i].requestCount)assert(request->list[i].offset==0);
        }
        request->callback(kIOReturnSuccess,request->list);
    }
}
static void completeIn(MockStreamPipe *in,MockFrameClock *clock,unsigned n) {
    MockRequest *request=in->pending[0];[in->pending removeObjectAtIndex:0];clock->now=request->frame+clock->delay;
    for(unsigned i=0;i<8;i++) {
        assert(request->list[i].requestCount==512 && request->list[i].offset==i*512);
        request->list[i].status=kIOReturnUnderrun;
        request->list[i].completeCount=(n%7 && (i%2))?176:0;
    }
    request->callback(kIOReturnSuccess,request->list);
}
int main(void) {@autoreleasepool {
    { /* Continuous service has a larger bounded runway; no deadline timer. */
        KoreNativeProbe *probe=[KoreNativeProbe new];MockFrameClock *clock=[MockFrameClock new];clock->now=1000;
        MockStreamPipe *in=[MockStreamPipe new],*out=[MockStreamPipe new];in->input=YES;
        probe->interface=(IOUSBHostInterface *)clock;probe->isoIn=(IOUSBHostPipe *)in;probe->isoOut=(IOUSBHostPipe *)out;
        probe->continuous=YES;probe->streamMode=YES;
        dispatch_sync(probe->queue,^{[probe startStream];});
        assert(in->pending.count==128 && probe->streamOuts.count==256 && probe->outLeadFrames==128 && !probe->streamTimer);
        dispatch_sync(probe->queue,^{completeIn(in,clock,0);});
        assert(out->pending.count==1 && out->pending[0]->frame==clock->now+128);
        dispatch_sync(probe->queue,^{
            for(unsigned n=1;n<200;n++) {
                clock->delay=n>=50?40:0; /* 40ms callback stall exceeds the former runway. */
                completeOut(out,in->pending[0]->frame+clock->delay);completeIn(in,clock,n);
                assert(!probe->failed);
            }
        });
        assert(probe->outSubmitted==200 && probe->inCompletedBatches==200);
        assert([probe stop]);
    }
    for(int mode=0;mode<3;mode++) {
        KoreNativeProbe *probe=[KoreNativeProbe new];
        MockFrameClock *clock=[MockFrameClock new];clock->now=1000;
        MockStreamPipe *in=[MockStreamPipe new],*out=[MockStreamPipe new];
        in->input=YES;
        probe->interface=(IOUSBHostInterface *)clock;probe->isoIn=(IOUSBHostPipe *)in;probe->isoOut=(IOUSBHostPipe *)out;
        probe->streamMode=YES;probe->streamDurationMs=3000;
        dispatch_sync(probe->queue,^{[probe startStream];});
        assert(in->pending.count==32 && out->pending.count==0);
        dispatch_sync(probe->queue,^{
            for(unsigned n=0;n<200;n++) {
                if(mode!=1)completeOut(out,in->pending[0]->frame);
                completeIn(in,clock,n);
                if(probe->failed)break;
            }
            if(mode==0) {
                assert(!probe->failed && probe->inCompletedBatches==200 && probe->outSubmitted==200);
                probe->streamDeadline=native_now_ns()-1;
                completeIn(in,clock,200);
                assert(probe->streamFinished && probe->stopping && !probe->failed);
            } else if(mode==1) {
                assert(probe->failed && probe->outSubmitted==64); /* bounded exhaustion fails, never drops */
            } else {
                out->reject=YES;completeIn(in,clock,200);assert(probe->failed);
            }
        });
        if(mode==0){in->deferredAbortQueue=probe->queue;out->deferredAbortQueue=probe->queue;}
        assert([probe stop]);
        assert(in->pending.count==0 && out->pending.count==0);
        for(KoreNativeSlot *slot in probe->streamIns)assert(!slot->busy);
        for(KoreNativeSlot *slot in probe->streamOuts)assert(!slot->busy);
    }
    puts("PASS native stream: 200 ordered batches, all-zero/trailing-zero slots, fixed frame cadence, ownership, pool exhaustion, rejection, deadline and cancellation");
}}
