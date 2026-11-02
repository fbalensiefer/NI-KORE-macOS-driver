/* SPDX-License-Identifier: GPL-2.0-or-later
 * Isolated IOUSBHost transport prototype; no CoreAudio device, MIDI or tones.
 * This process owns interface 0 exclusively. Never run beside the libusb bridge.
 */
#import <Foundation/Foundation.h>
#import <IOUSBHost/IOUSBHost.h>
#include "native_plan.h"
#include "native_discovery.h"
#include <signal.h>
#import <IOKit/usb/USB.h>
static volatile sig_atomic_t interrupted;
static void on_signal(int sig) {(void)sig;interrupted=1;}
static void report_error(const char *where,NSError *error) {
    fprintf(stderr,"NATIVE %s: domain=%s code=0x%08x description=%s\n",where,
            error.domain.UTF8String,(unsigned)error.code,error.localizedDescription.UTF8String);
}
@interface KoreNativeProbe : NSObject {
@public
    dispatch_queue_t queue;
    dispatch_semaphore_t done;
    IOUSBHostInterface *interface;
    IOUSBHostPipe *bulkIn,*bulkOut,*isoIn,*isoOut;
    NSMutableData *bulkData,*inData,*outData;
    IOUSBHostIsochronousTransaction inList[8],outList[8];
    int expected,profile,zeroAtStart,failed;
    unsigned guardBytes;
    BOOL stopping,replySeen,outCompleted;
    unsigned char reply[512];
    NSUInteger replyLength;
}
- (BOOL)open;
- (void)armBulk;
- (BOOL)exchange:(const unsigned char *)bytes length:(NSUInteger)length reply:(int)command;
- (void)startISO;
- (BOOL)waitForCompletion;
- (BOOL)stop;
@end
@implementation KoreNativeProbe
- (instancetype)init {
    if((self=[super init])) {
        queue=dispatch_queue_create("kore.native.usb",DISPATCH_QUEUE_SERIAL);
        done=dispatch_semaphore_create(0);bulkData=[NSMutableData dataWithLength:512];
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
    BOOL submitted=[bulkIn enqueueIORequestWithData:bulkData completionTimeout:0.5 error:&error
        completionHandler:^(IOReturn status,NSUInteger count) {
        if(self->stopping)return;
        if(status!=kIOReturnSuccess || count>512) {[self finishFailure:"bulk IN completion" error:nil];return;}
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
            dispatch_sync(queue,^{self->stopping=YES;});
            NSError *error=nil;
            if(![bulkIn abortWithOption:IOUSBHostAbortOptionSynchronous error:&error]) {
                report_error("bulk retry abort",error);return NO;
            }
            dispatch_sync(queue,^{self->stopping=NO;self->failed=0;});
            while(!dispatch_semaphore_wait(done,DISPATCH_TIME_NOW)) {}
            printf("NATIVE GET_DEVICE_INFO retry=%u/3\n",attempt);
        }
        dispatch_sync(queue,^{
            self->expected=command;self->replySeen=NO;self->replyLength=0;
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
- (BOOL)stop {
    dispatch_sync(queue,^{self->stopping=YES;});
    BOOL success=YES;
    for(IOUSBHostPipe *pipe in @[bulkIn?:[NSNull null],bulkOut?:[NSNull null],isoIn?:[NSNull null],isoOut?:[NSNull null]]) {
        if((id)pipe==[NSNull null])continue;
        NSError *error=nil;
        if(![pipe abortWithOption:IOUSBHostAbortOptionSynchronous error:&error]) {
            report_error("synchronous abort",error);success=NO;
        }
    }
    if(!success)return NO; /* caller exits without releasing active backing storage */
    dispatch_sync(queue,^{});
    [interface destroy];dispatch_sync(queue,^{});
    puts("NATIVE transport closed");return YES;
}
@end
int main(int argc,const char **argv) {
    @autoreleasepool {
        int profile=caiaq_probe_id("observed"),zeroAtStart=1;unsigned guard=1;BOOL run=NO,discover=NO;
        for(int i=1;i<argc;i++) {
            if(!strcmp(argv[i],"--discover"))discover=YES;
            else if(!strcmp(argv[i],"--run"))run=YES;
            else if(!strcmp(argv[i],"--dry-run"))run=NO;
            else if(!strcmp(argv[i],"--layout") && i+1<argc) {
                const char *name=argv[++i];profile=!strcmp(name,"rx")?-1:caiaq_probe_id(name);
                if(profile<0 && strcmp(name,"rx"))return 2;
            } else if(!strcmp(argv[i],"--zero-offset") && i+1<argc) {
                const char *name=argv[++i];if(strcmp(name,"start") && strcmp(name,"compact"))return 2;
                zeroAtStart=!strcmp(name,"start");
            } else if(!strcmp(argv[i],"--buffer-guard") && i+1<argc) {
                const char *n=argv[++i];if(strcmp(n,"0") && strcmp(n,"1"))return 2;guard=(unsigned)(n[0]-'0');
            } else {fprintf(stderr,"Usage: %s [--run|--dry-run|--discover] [--layout observed|rx|PROFILE] [--zero-offset start|compact] [--buffer-guard 0|1]\n",argv[0]);return 2;}
        }
        setvbuf(stdout,NULL,_IOLBF,0);
        printf("NATIVE CONFIG hardware=%s layout=%s zero-offset=%s backing-guard=%u (not transmitted), PCM=silence, one batch\n",
            run?"ENABLED":"disabled",profile<0?"rx":caiaq_probe_names[profile],zeroAtStart?"start":"compact",guard);
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
        KoreNativeProbe *probe=[KoreNativeProbe new];probe->profile=profile;probe->zeroAtStart=zeroAtStart;probe->guardBytes=guard;
        int rc=1;
        if([probe open]) {
            const unsigned char info[]={1},params[]={9,1,2,176,0,1};
            if([probe exchange:info length:sizeof(info) reply:1]) {
                const unsigned char *p=probe->reply+1;
                if(probe->replyLength>=15 && p[7]==4 && p[8]==2 && p[9]==0 && p[10]==0 && p[13]==2) {
                    if([probe exchange:params length:sizeof(params) reply:9] && probe->replyLength>=2 && probe->reply[1]==1) {
                        puts("NATIVE AUDIO_PARAMS accepted");
                        dispatch_sync(probe->queue,^{[probe startISO];});
                        if([probe waitForCompletion]) {
                            dispatch_sync(probe->queue,^{/* queue barrier before inspecting result */});
                            if(probe->outCompleted && !probe->failed)rc=0;
                        }
                    } else fprintf(stderr,"NATIVE AUDIO_PARAMS not accepted; no ISO started\n");
                } else fprintf(stderr,"NATIVE unexpected device specification; no AUDIO_PARAMS sent\n");
            }
        }
        if(![probe stop]) {fflush(NULL);_Exit(1);}
        return rc;
    }
}
