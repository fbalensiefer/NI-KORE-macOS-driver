/* SPDX-License-Identifier: GPL-2.0-or-later
 * Public CoreAudio property IPC; runs off the USB and audio realtime threads.
 */
#import <CoreAudio/CoreAudio.h>
#import <Foundation/Foundation.h>
#include "pcm_ring.h"
#include <string.h>
static inline AudioDeviceID kore_coreaudio_find(void) {
    AudioObjectPropertyAddress a={kAudioHardwarePropertyTranslateUIDToDevice,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    CFStringRef uid=CFSTR("org.nikore.experimental.output");AudioDeviceID device=kAudioObjectUnknown;UInt32 size=sizeof(device);
    if(AudioObjectGetPropertyData(kAudioObjectSystemObject,&a,sizeof(uid),&uid,&size,&device))return kAudioObjectUnknown;
    return device;
}
static inline OSStatus kore_coreaudio_lease(AudioDeviceID device,BOOL own) {
    AudioObjectPropertyAddress a={'kown',kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    CFBooleanRef value=own?kCFBooleanTrue:kCFBooleanFalse;
    return AudioObjectSetPropertyData(device,&a,0,NULL,sizeof(value),&value);
}
static inline OSStatus kore_coreaudio_pull(AudioDeviceID device,struct kore_pcm_ring *queue) {
    uint64_t w=atomic_load_explicit(&queue->written,memory_order_acquire),r=atomic_load_explicit(&queue->read,memory_order_acquire);
    if(w-r>2048)return 0;
    AudioObjectPropertyAddress a={'kpcm',kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    CFDataRef data=NULL;UInt32 size=sizeof(data);
    OSStatus status=AudioObjectGetPropertyData(device,&a,0,NULL,&size,&data);
    if(status)return status;
    if(!data || CFGetTypeID(data)!=CFDataGetTypeID() || CFDataGetLength(data)%8 || CFDataGetLength(data)>4096) {
        if(data)CFRelease(data);return kAudioHardwareBadPropertySizeError;
    }
    float aligned[1024];memcpy(aligned,CFDataGetBytePtr(data),(size_t)CFDataGetLength(data));
    kore_pcm_push(queue,aligned,(size_t)CFDataGetLength(data)/8);CFRelease(data);return 0;
}
