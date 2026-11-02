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
    OSStatus status=AudioObjectGetPropertyData(kAudioObjectSystemObject,&a,sizeof(uid),&uid,&size,&device);
    if(!status && device)return device;
    a.mSelector=kAudioHardwarePropertyDevices;size=0;
    status=AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&a,0,NULL,&size);
    if(status || size>1024*sizeof(AudioDeviceID)) {fprintf(stderr,"CoreAudio device enumeration status=%d (sandbox/audio service access may be unavailable)\n",status);return kAudioObjectUnknown;}
    AudioDeviceID devices[1024];
    if(AudioObjectGetPropertyData(kAudioObjectSystemObject,&a,0,NULL,&size,devices))return kAudioObjectUnknown;
    AudioObjectPropertyAddress name={kAudioDevicePropertyDeviceUID,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    for(unsigned i=0;i<size/sizeof(AudioDeviceID);i++) {
        CFStringRef found=NULL;UInt32 length=sizeof(found);
        if(!AudioObjectGetPropertyData(devices[i],&name,0,NULL,&length,&found) && found) {
            BOOL match=CFEqual(found,uid);CFRelease(found);if(match)return devices[i];
        }
    }
    return kAudioObjectUnknown;
}
static inline OSStatus kore_coreaudio_lease(AudioDeviceID device,BOOL own) {
    AudioObjectPropertyAddress a={'kown',kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    CFBooleanRef value=own?kCFBooleanTrue:kCFBooleanFalse;
    return AudioObjectSetPropertyData(device,&a,0,NULL,sizeof(value),&value);
}
static inline OSStatus kore_coreaudio_pull(AudioDeviceID device,struct kore_pcm_ring *queue) {
    uint64_t w=atomic_load_explicit(&queue->written,memory_order_acquire),r=atomic_load_explicit(&queue->read,memory_order_acquire);
    if(w-r>2048)return 0;
    AudioObjectPropertyAddress a={'kpc4',kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    CFDataRef data=NULL;UInt32 size=sizeof(data);
    OSStatus status=AudioObjectGetPropertyData(device,&a,0,NULL,&size,&data);
    if(status)return status;
    if(!data || CFGetTypeID(data)!=CFDataGetTypeID() || CFDataGetLength(data)%16 || CFDataGetLength(data)>8192) {
        if(data)CFRelease(data);return kAudioHardwareBadPropertySizeError;
    }
    if(kore_pcm_channels(queue)!=4){CFRelease(data);return kAudioHardwareBadPropertySizeError;}
    float aligned[2048];memcpy(aligned,CFDataGetBytePtr(data),(size_t)CFDataGetLength(data));
    kore_pcm_push(queue,aligned,(size_t)CFDataGetLength(data)/16);CFRelease(data);return 0;
}
