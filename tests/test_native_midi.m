/* Actual native bulk callbacks and decoder; CoreMIDI delivery replaced by a recorder. */
#import <CoreMIDI/CoreMIDI.h>
#include <assert.h>
static unsigned events;
static unsigned char messages[64][3];
static OSStatus recorded_midi(MIDIEndpointRef endpoint,const MIDIPacketList *list) {
    assert(endpoint==1 && list->numPackets==1 && list->packet[0].length==3);
    assert(events<64);memcpy(messages[events++],list->packet[0].data,3);return 0;
}
#define MIDIReceived recorded_midi
#define main native_probe_main
#include "../audio/native_probe.m"
#undef main
#undef MIDIReceived
@interface MidiBulk : NSObject {
@public IOUSBHostCompletionHandler callback;NSMutableData *buffer;unsigned submissions;
}
@end
@implementation MidiBulk
- (BOOL)enqueueIORequestWithData:(NSMutableData *)data completionTimeout:(NSTimeInterval)timeout error:(NSError **)error completionHandler:(IOUSBHostCompletionHandler)handler {
    (void)error;assert(timeout==0.5 && !callback);buffer=data;callback=handler;submissions++;return YES;
}
- (BOOL)abortWithOption:(IOUSBHostAbortOption)option error:(NSError **)error {
    (void)error;assert(option==IOUSBHostAbortOptionSynchronous);
    if(callback){IOUSBHostCompletionHandler handler=callback;callback=nil;handler(kIOReturnAborted,0);}return YES;
}
- (void)packet:(const unsigned char *)bytes count:(unsigned)count status:(IOReturn)status {
    assert(callback);if(count)memcpy(buffer.mutableBytes,bytes,count);
    IOUSBHostCompletionHandler handler=callback;callback=nil;handler(status,count);
}
@end
int main(void) {@autoreleasepool {
    KoreNativeProbe *probe=[KoreNativeProbe new];MidiBulk *pipe=[MidiBulk new];
    probe->bulkIn=(IOUSBHostPipe *)pipe;probe->midiMode=YES;probe->expected=-1;probe->controllerActive=YES;
    source=1; /* Only the recorder sees this synthetic endpoint. */
    dispatch_semaphore_t blocked=dispatch_semaphore_create(0),entered=dispatch_semaphore_create(0);
    dispatch_async(probe->controllerQueue,^{dispatch_semaphore_signal(entered);dispatch_semaphore_wait(blocked,DISPATCH_TIME_FOREVER);});
    dispatch_semaphore_wait(entered,DISPATCH_TIME_FOREVER);
    dispatch_sync(probe->queue,^{
        [probe armBulk];[probe armBulk];assert(pipe->submissions==1); /* one buffer owner */
        const unsigned char baseline[]={4,0,0,0,0},pressed[]={4,1,0,0,0},released[]={4,0,0,0,0};
        [pipe packet:baseline count:sizeof(baseline) status:0];
        [pipe packet:pressed count:sizeof(pressed) status:0];
        [pipe packet:released count:sizeof(released) status:0];
        [pipe packet:NULL count:0 status:kIOReturnTimeout]; /* an idle controller is not a failure */
        assert(!probe->failed && probe->bulkPending && pipe->submissions==5);
    });
    // The feedback queue is still blocked: MIDI decode must complete independently.
    dispatch_sync(probe->midiQueue,^{});
    assert(events==2 && messages[0][0]==0x90 && messages[0][1]==36 && messages[0][2]==100);
    assert(messages[1][0]==0x80 && messages[1][1]==36 && messages[1][2]==0);
    assert(probe->midiPackets==3 && atomic_load(&probe->midiBacklog)==0);
    // Actual IO decoder: 8-bit wheel counter, baseline, wrap, reverse and split.
    events=0;io_misc_last=-1;
    unsigned char wheel[]={4,0,0,0,0,1};process(wheel,sizeof(wheel));assert(events==0);
    wheel[5]=0;process(wheel,sizeof(wheel));assert(events==1 && messages[0][1]==28 && messages[0][2]==1);
    wheel[5]=255;process(wheel,sizeof(wheel));assert(events==2 && messages[1][2]==1);
    wheel[5]=0;process(wheel,sizeof(wheel));assert(events==3 && messages[2][2]==65);
    wheel[5]=128;process(wheel,sizeof(wheel));assert(events==3); // ambiguous half-turn
    wheel[5]=58;process(wheel,sizeof(wheel));assert(events==5 && messages[3][2]==63 && messages[4][2]==7);
    dispatch_semaphore_signal(blocked);source=0;
    assert([probe stop] && !probe->bulkPending);
    pipe->buffer=nil;probe->bulkIn=nil;
    puts("PASS shared native MIDI: bulk ownership, timeout rearm, note press/release, independent feedback queue, cancellation");
}}
