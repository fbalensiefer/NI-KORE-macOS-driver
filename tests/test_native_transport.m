/* Exercise actual native callbacks using mock pipes; no IOKit object opened. */
#define main native_probe_main
#include "../audio/native_probe.m"
#undef main
#include <assert.h>
@interface FakeNativePipe : NSObject {
@public
    BOOL input,reject;
    unsigned submissions,aborts;
    KoreNativeProbe *owner;
}
@end
@implementation FakeNativePipe
- (BOOL)enqueueIORequestWithData:(NSMutableData *)data
               transactionList:(IOUSBHostIsochronousTransaction *)list
          transactionListCount:(NSUInteger)count firstFrameNumber:(uint64_t)first
                       options:(IOUSBHostIsochronousTransferOptions)options error:(NSError **)error
             completionHandler:(IOUSBHostIsochronousTransactionCompletionHandler)callback {
    assert(count==8 && first==0 && options==0);submissions++;
    if(reject) {*error=[NSError errorWithDomain:IOUSBHostErrorDomain code:kIOReturnBadArgument userInfo:nil];return NO;}
    unsigned observed[8]={0,176,176,0,176,0,176,0};
    for(unsigned i=0;i<8;i++) {
        if(input) {
            assert(data.length==4096 && list[i].offset==i*512 && list[i].requestCount==512);
            list[i].completeCount=observed[i];list[i].status=kIOReturnUnderrun;
        } else {
            assert(list[i].requestCount==observed[i]);
            if(!observed[i] && owner->zeroAtStart)assert(list[i].offset==0);
            assert((size_t)list[i].offset+list[i].requestCount<=data.length);
            list[i].completeCount=list[i].requestCount;list[i].status=kIOReturnSuccess;
        }
    }
    callback(kIOReturnSuccess,list);return YES;
}
- (BOOL)abortWithOption:(IOUSBHostAbortOption)option error:(NSError **)error {
    (void)error;assert(option==IOUSBHostAbortOptionSynchronous);aborts++;return YES;
}
@end
@interface DeferredBulkPipe : NSObject {
@public NSMutableArray<IOUSBHostCompletionHandler> *callbacks;
}
@end
@implementation DeferredBulkPipe
- (instancetype)init {if((self=[super init]))callbacks=[NSMutableArray array];return self;}
- (BOOL)enqueueIORequestWithData:(NSMutableData *)data completionTimeout:(NSTimeInterval)timeout
                         error:(NSError **)error completionHandler:(IOUSBHostCompletionHandler)callback {
    (void)data;(void)timeout;(void)error;[callbacks addObject:callback];return YES;
}
@end
int main(void) {
    @autoreleasepool {
        assert(caiaq_native_identity(@{@"idVendor":@0x17cc,@"idProduct":@0x4711,@"bInterfaceNumber":@0}));
        assert(!caiaq_native_identity(@{@"idVendor":@0x17cc,@"idProduct":@0x4712,@"bInterfaceNumber":@0}));
        assert(!caiaq_native_identity(@{@"idVendor":@0x17cc,@"idProduct":@0x4711,@"bInterfaceNumber":@1}));
        assert(!caiaq_native_identity(@{@"idVendor":@0x17cc,@"idProduct":@0x4711}));
        assert(!caiaq_native_identity(@{@"idVendor":@"6092",@"idProduct":@0x4711,@"bInterfaceNumber":@0}));
        KoreNativeProbe *bulkProbe=[KoreNativeProbe new];DeferredBulkPipe *bulk=[DeferredBulkPipe new];
        bulkProbe->bulkIn=(IOUSBHostPipe *)bulk;
        dispatch_sync(bulkProbe->queue,^{
            bulkProbe->expected=9;bulkProbe->bulkGeneration=1;[bulkProbe armBulk];
            bulkProbe->bulkGeneration=2;bulkProbe->bulkPending=NO; /* synchronous retry abort released kernel buffer */
            [bulkProbe armBulk];
            bulk->callbacks[0](kIOReturnAborted,0);assert(!bulkProbe->failed && !bulkProbe->replySeen);
            unsigned char *bytes=bulkProbe->bulkData.mutableBytes;bytes[0]=9;bytes[1]=1;
            bulk->callbacks[1](kIOReturnSuccess,2);assert(bulkProbe->replySeen && bulkProbe->replyLength==2);
        });
        [bulk->callbacks removeAllObjects];bulkProbe->bulkIn=nil;
        for(int mode=0;mode<3;mode++) {
            KoreNativeProbe *probe=[KoreNativeProbe new];
            FakeNativePipe *in=[FakeNativePipe new],*out=[FakeNativePipe new];
            in->input=YES;out->owner=probe;out->reject=(mode==2);
            probe->isoIn=(IOUSBHostPipe *)in;probe->isoOut=(IOUSBHostPipe *)out;
            probe->profile=caiaq_probe_id("observed");probe->zeroAtStart=(mode!=0);probe->guardBytes=1;
            dispatch_sync(probe->queue,^{[probe startISO];});
            assert([probe waitForCompletion]);
            dispatch_sync(probe->queue,^{});
            assert(in->submissions==1 && out->submissions==1);
            assert(probe->outCompleted==(mode!=2) && !!probe->failed==(mode==2));
            assert([probe stop] && in->aborts==1 && out->aborts==1);
            out->owner=nil; /* break test-only ownership cycle */
        }
        puts("PASS native transport callbacks: exact trailing-zero layout, completion, rejection and synchronous cleanup (mock only)");
    }
}
