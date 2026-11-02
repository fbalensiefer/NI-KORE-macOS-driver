/* SPDX-License-Identifier: GPL-2.0-or-later
 * Experimental fixed 48k four-channel virtual output. USB stays in a separate process.
 * Host-marshalled CFData custom property transports PCM; no shared files/socket.
 */
#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreAudio/HostTime.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <math.h>
#include <os/log.h>
#include <string.h>
#include "pcm_ring.h"
#define KORE_CLOCK_PERIOD 16384u
#define KORE_BOX 4u
#define KORE_DEVICE 2u
#define KORE_STREAM 3u
#define KORE_PULL 'kpc4'
#define KORE_LEASE 'kown'
#define KORE_UID CFSTR("org.nikore.experimental.output")
static struct kore_pcm_ring pcm={.channels=4};
static pthread_mutex_t control=PTHREAD_MUTEX_INITIALIZER;
static _Atomic unsigned running,refs=1;
static _Atomic uint64_t anchor,clockSeed=1;
static pid_t consumer;
static AudioServerPlugInDriverInterface driver;
static AudioServerPlugInDriverInterface *driverPtr=&driver;
static AudioServerPlugInDriverRef driverRef=&driverPtr;
static AudioStreamBasicDescription format(void) {
    return (AudioStreamBasicDescription){.mSampleRate=48000,.mFormatID=kAudioFormatLinearPCM,
        .mFormatFlags=kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked|kAudioFormatFlagsNativeEndian,
        .mBytesPerPacket=16,.mFramesPerPacket=1,.mBytesPerFrame=16,.mChannelsPerFrame=4,.mBitsPerChannel=32};
}
static HRESULT query(void *d,REFIID uuid,LPVOID *out) {
    if(d!=driverRef || !out)return E_NOINTERFACE;
    CFUUIDRef id=CFUUIDCreateFromUUIDBytes(NULL,uuid);
    Boolean yes=CFEqual(id,IUnknownUUID) || CFEqual(id,kAudioServerPlugInDriverInterfaceUUID);CFRelease(id);
    *out=yes?driverRef:NULL;if(yes)atomic_fetch_add(&refs,1);return yes?S_OK:E_NOINTERFACE;
}
static ULONG add_ref(void *d){return d==driverRef?atomic_fetch_add(&refs,1)+1:0;}
static ULONG release_ref(void *d){if(d!=driverRef)return 0;unsigned n=atomic_load(&refs);while(n && !atomic_compare_exchange_weak(&refs,&n,n-1)){}return n?n-1:0;}
static OSStatus initialize(AudioServerPlugInDriverRef d,AudioServerPlugInHostRef host){(void)host;return d==driverRef?0:kAudioHardwareBadObjectError;}
static OSStatus create(AudioServerPlugInDriverRef d,CFDictionaryRef desc,const AudioServerPlugInClientInfo *client,AudioObjectID *out){(void)d;(void)desc;(void)client;(void)out;return kAudioHardwareUnsupportedOperationError;}
static OSStatus destroy(AudioServerPlugInDriverRef d,AudioObjectID id){(void)d;(void)id;return kAudioHardwareUnsupportedOperationError;}
static OSStatus client(AudioServerPlugInDriverRef d,AudioObjectID id,const AudioServerPlugInClientInfo *c){(void)c;return d==driverRef && id==KORE_DEVICE?0:kAudioHardwareBadObjectError;}
static OSStatus config(AudioServerPlugInDriverRef d,AudioObjectID id,UInt64 action,void *info){(void)action;(void)info;return d==driverRef && id==KORE_DEVICE?0:kAudioHardwareBadObjectError;}
static Boolean valid_object(AudioObjectID id){return id==kAudioObjectPlugInObject || id==KORE_DEVICE || id==KORE_STREAM || id==KORE_BOX;}
static Boolean has(AudioServerPlugInDriverRef d,AudioObjectID id,pid_t pid,const AudioObjectPropertyAddress *a) {
    (void)pid;if(d!=driverRef || !a || !valid_object(id))return false;
    if(a->mSelector==kAudioObjectPropertyElementName)return id==KORE_DEVICE && a->mScope==kAudioObjectPropertyScopeOutput && a->mElement>=1 && a->mElement<=4;
    switch(a->mSelector) {
        case kAudioObjectPropertyBaseClass:case kAudioObjectPropertyClass:case kAudioObjectPropertyOwner:
        case kAudioObjectPropertyModelName:case kAudioObjectPropertyName:case kAudioObjectPropertyManufacturer:case kAudioObjectPropertyOwnedObjects:return true;
    }
    if(id==kAudioObjectPlugInObject)switch(a->mSelector){case kAudioPlugInPropertyDeviceList:case kAudioPlugInPropertyTranslateUIDToDevice:case kAudioPlugInPropertyBundleID:case kAudioPlugInPropertyBoxList:case kAudioPlugInPropertyTranslateUIDToBox:case kAudioPlugInPropertyResourceBundle:return true;}
    if(id==KORE_BOX)switch(a->mSelector) {
        case kAudioBoxPropertyBoxUID:case kAudioBoxPropertyTransportType:case kAudioBoxPropertyHasAudio:
        case kAudioBoxPropertyHasVideo:case kAudioBoxPropertyHasMIDI:case kAudioBoxPropertyIsProtected:
        case kAudioBoxPropertyAcquired:case kAudioBoxPropertyAcquisitionFailed:case kAudioBoxPropertyDeviceList:
        case kAudioBoxPropertyClockDeviceList:return true;
    }
    if(id==KORE_DEVICE)switch(a->mSelector) {
        case kAudioObjectPropertyCustomPropertyInfoList:case KORE_PULL:case KORE_LEASE:
        case kAudioDevicePropertyDeviceUID:case kAudioDevicePropertyModelUID:case kAudioDevicePropertyTransportType:
        case kAudioDevicePropertyRelatedDevices:case kAudioDevicePropertyClockDomain:case kAudioDevicePropertyDeviceIsAlive:
        case kAudioDevicePropertyDeviceIsRunning:case kAudioDevicePropertyDeviceCanBeDefaultDevice:case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
        case kAudioDevicePropertyLatency:case kAudioDevicePropertySafetyOffset:case kAudioDevicePropertyStreams:
        case kAudioObjectPropertyControlList:case kAudioDevicePropertyNominalSampleRate:case kAudioDevicePropertyAvailableNominalSampleRates:
        case kAudioDevicePropertyClockAlgorithm:case kAudioDevicePropertyClockIsStable:case kAudioDevicePropertyIsHidden:
        case kAudioDevicePropertyZeroTimeStampPeriod:case kAudioDevicePropertyPreferredChannelsForStereo:return true;
    }
    if(id==KORE_STREAM)switch(a->mSelector) {
        case kAudioStreamPropertyIsActive:case kAudioStreamPropertyDirection:case kAudioStreamPropertyTerminalType:
        case kAudioStreamPropertyStartingChannel:case kAudioStreamPropertyLatency:case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:case kAudioStreamPropertyAvailableVirtualFormats:case kAudioStreamPropertyAvailablePhysicalFormats:return true;
    }
    static _Atomic unsigned unknown;
    if(atomic_fetch_add(&unknown,1)<128)os_log(OS_LOG_DEFAULT,"KORE HAL unsupported object=%u selector=%08x scope=%08x",id,a->mSelector,a->mScope);
    return false;
}
static OSStatus settable(AudioServerPlugInDriverRef d,AudioObjectID id,pid_t pid,const AudioObjectPropertyAddress *a,Boolean *out) {
    if(!out || !has(d,id,pid,a))return kAudioHardwareUnknownPropertyError;
    *out=id==KORE_DEVICE && a->mSelector==KORE_LEASE;return 0;
}
static UInt32 property_size(AudioObjectID id,const AudioObjectPropertyAddress *a) {
    switch(a->mSelector) {
        case kAudioObjectPropertyElementName:case kAudioPlugInPropertyResourceBundle:case kAudioObjectPropertyModelName:case kAudioObjectPropertyName:case kAudioObjectPropertyManufacturer:case kAudioPlugInPropertyBundleID:
        case kAudioDevicePropertyDeviceUID:case kAudioDevicePropertyModelUID:case kAudioBoxPropertyBoxUID:case KORE_PULL:case KORE_LEASE:return sizeof(CFTypeRef);
        case kAudioObjectPropertyOwnedObjects:return id==kAudioObjectPlugInObject?2*sizeof(AudioObjectID):id==KORE_BOX || id==KORE_STREAM || (id==KORE_DEVICE && a->mScope==kAudioObjectPropertyScopeInput)?0:sizeof(AudioObjectID);
        case kAudioBoxPropertyClockDeviceList:return 0;
        case kAudioPlugInPropertyBoxList:case kAudioBoxPropertyDeviceList:case kAudioPlugInPropertyDeviceList:case kAudioDevicePropertyRelatedDevices:return sizeof(AudioObjectID);
        case kAudioDevicePropertyStreams:return a->mScope==kAudioObjectPropertyScopeInput?0:sizeof(AudioObjectID);
        case kAudioObjectPropertyControlList:return 0;
        case kAudioDevicePropertyNominalSampleRate:return sizeof(Float64);
        case kAudioDevicePropertyAvailableNominalSampleRates:return sizeof(AudioValueRange);
        case kAudioDevicePropertyPreferredChannelsForStereo:return 2*sizeof(UInt32);
        case kAudioStreamPropertyVirtualFormat:case kAudioStreamPropertyPhysicalFormat:return sizeof(AudioStreamBasicDescription);
        case kAudioStreamPropertyAvailableVirtualFormats:case kAudioStreamPropertyAvailablePhysicalFormats:return sizeof(AudioStreamRangedDescription);
        case kAudioObjectPropertyCustomPropertyInfoList:return 2*sizeof(AudioServerPlugInCustomPropertyInfo);
        default:return sizeof(UInt32);
    }
}
static OSStatus get_size(AudioServerPlugInDriverRef d,AudioObjectID id,pid_t pid,const AudioObjectPropertyAddress *a,UInt32 qs,const void *q,UInt32 *out) {
    (void)qs;(void)q;if(!out || !has(d,id,pid,a))return kAudioHardwareUnknownPropertyError;*out=property_size(id,a);return 0;
}
static OSStatus get(AudioServerPlugInDriverRef d,AudioObjectID id,pid_t pid,const AudioObjectPropertyAddress *a,UInt32 qs,const void *q,UInt32 capacity,UInt32 *out,void *data) {
    if(!out || !has(d,id,pid,a)){if(a)os_log(OS_LOG_DEFAULT,"KORE HAL failed GET object=%u selector=%08x",id,a->mSelector);return kAudioHardwareUnknownPropertyError;}
    UInt32 size=property_size(id,a);if(capacity<size || (size && !data))return kAudioHardwareBadPropertySizeError;*out=size;
    UInt32 value=0;CFStringRef string=NULL;
    switch(a->mSelector) {
        case kAudioObjectPropertyBaseClass:value=kAudioObjectClassID;break;
        case kAudioObjectPropertyClass:value=id==kAudioObjectPlugInObject?kAudioPlugInClassID:id==KORE_DEVICE?kAudioDeviceClassID:id==KORE_BOX?kAudioBoxClassID:kAudioStreamClassID;break;
        case kAudioObjectPropertyOwner:value=id==kAudioObjectPlugInObject?kAudioObjectUnknown:(id==KORE_DEVICE || id==KORE_BOX)?kAudioObjectPlugInObject:KORE_DEVICE;break;
        case kAudioObjectPropertyElementName: {
            CFStringRef names[]={CFSTR("Main Left"),CFSTR("Main Right"),CFSTR("Headphones Left"),CFSTR("Headphones Right")};
            string=names[a->mElement-1];break;
        }
        case kAudioObjectPropertyModelName:string=CFSTR("KORE 1 Native Output");break;
        case kAudioObjectPropertyName:string=id==KORE_DEVICE?CFSTR("KORE 1 Experimental Output"):CFSTR("KORE 1 Native Bridge");break;
        case kAudioObjectPropertyManufacturer:string=CFSTR("NI-KORE open-source project");break;
        case kAudioPlugInPropertyResourceBundle:string=CFSTR("");break;
        case kAudioPlugInPropertyBundleID:string=CFSTR("org.nikore.experimental.driver");break;
        case kAudioDevicePropertyDeviceUID:string=KORE_UID;break;
        case kAudioBoxPropertyBoxUID:string=CFSTR("org.nikore.experimental.box");break;
        case kAudioDevicePropertyModelUID:string=CFSTR("org.nikore.experimental.model");break;
        case kAudioObjectPropertyOwnedObjects:if(!size)return 0;if(id==kAudioObjectPlugInObject){AudioObjectID objects[2]={KORE_BOX,KORE_DEVICE};memcpy(data,objects,sizeof(objects));return 0;}value=id==KORE_DEVICE?KORE_STREAM:KORE_DEVICE;break;
        case kAudioPlugInPropertyBoxList:value=KORE_BOX;break;
        case kAudioBoxPropertyClockDeviceList:return 0;
        case kAudioBoxPropertyHasAudio:case kAudioBoxPropertyAcquired:value=1;break;
        case kAudioBoxPropertyHasVideo:case kAudioBoxPropertyHasMIDI:case kAudioBoxPropertyIsProtected:case kAudioBoxPropertyAcquisitionFailed:value=0;break;
        case kAudioBoxPropertyDeviceList:case kAudioPlugInPropertyDeviceList:case kAudioDevicePropertyRelatedDevices:value=KORE_DEVICE;break;
        case kAudioPlugInPropertyTranslateUIDToBox:case kAudioPlugInPropertyTranslateUIDToDevice:
            if(qs!=sizeof(CFStringRef) || !q || !*(CFStringRef const *)q || CFGetTypeID(*(CFTypeRef const *)q)!=CFStringGetTypeID())return kAudioHardwareBadPropertySizeError;
            value=a->mSelector==kAudioPlugInPropertyTranslateUIDToBox?(CFEqual(*(CFStringRef const *)q,CFSTR("org.nikore.experimental.box"))?KORE_BOX:kAudioObjectUnknown):(CFEqual(*(CFStringRef const *)q,KORE_UID)?KORE_DEVICE:kAudioObjectUnknown);break;
        case kAudioDevicePropertyTransportType:value=kAudioDeviceTransportTypeVirtual;break;
        case kAudioDevicePropertyDeviceIsAlive:value=1;break;
        case kAudioDevicePropertyDeviceIsRunning:value=atomic_load(&running)>0;break;
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:value=0;break;
        case kAudioDevicePropertyStreams:if(!size)return 0;value=KORE_STREAM;break;
        case kAudioObjectPropertyControlList:return 0;
        case kAudioDevicePropertyNominalSampleRate:*(Float64 *)data=48000;return 0;
        case kAudioDevicePropertyAvailableNominalSampleRates:*(AudioValueRange *)data=(AudioValueRange){48000,48000};return 0;
        case kAudioDevicePropertyLatency:value=6144;break;
        case kAudioDevicePropertySafetyOffset:value=2048;break;
        case kAudioDevicePropertyClockAlgorithm:value=kAudioDeviceClockAlgorithmRaw;break;
        case kAudioDevicePropertyClockIsStable:value=1;break;
        case kAudioDevicePropertyIsHidden:value=0;break;
        case kAudioDevicePropertyZeroTimeStampPeriod:value=KORE_CLOCK_PERIOD;break;
        case kAudioDevicePropertyPreferredChannelsForStereo:((UInt32 *)data)[0]=1;((UInt32 *)data)[1]=2;return 0;
        case kAudioStreamPropertyIsActive:value=1;break;
        case kAudioStreamPropertyDirection:value=0;break;
        case kAudioStreamPropertyTerminalType:value=kAudioStreamTerminalTypeLine;break;
        case kAudioStreamPropertyStartingChannel:value=1;break;
        case kAudioStreamPropertyVirtualFormat:case kAudioStreamPropertyPhysicalFormat:*(AudioStreamBasicDescription *)data=format();return 0;
        case kAudioStreamPropertyAvailableVirtualFormats:case kAudioStreamPropertyAvailablePhysicalFormats:
            *(AudioStreamRangedDescription *)data=(AudioStreamRangedDescription){format(),{48000,48000}};return 0;
        case kAudioObjectPropertyCustomPropertyInfoList: {
            AudioServerPlugInCustomPropertyInfo values[2]={{KORE_PULL,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,kAudioServerPlugInCustomPropertyDataTypeNone},
                {KORE_LEASE,kAudioServerPlugInCustomPropertyDataTypeCFPropertyList,kAudioServerPlugInCustomPropertyDataTypeNone}};
            memcpy(data,values,sizeof(values));return 0;
        }
        case KORE_LEASE: pthread_mutex_lock(&control);*(CFTypeRef *)data=CFRetain(consumer==pid?kCFBooleanTrue:kCFBooleanFalse);pthread_mutex_unlock(&control);return 0;
        case KORE_PULL: {
            pthread_mutex_lock(&control);
            if(!consumer || consumer!=pid){pthread_mutex_unlock(&control);return kAudioHardwareIllegalOperationError;}
            float block[512*4];size_t frames=kore_pcm_pop(&pcm,block,512);
            *(CFDataRef *)data=CFDataCreate(NULL,(const UInt8 *)block,(CFIndex)(frames*16));
            pthread_mutex_unlock(&control);return *(CFDataRef *)data?0:kAudioHardwareUnspecifiedError;
        }
        default:break; /* zero latency/safety offset/clock domain */
    }
    if(string)*(CFStringRef *)data=CFRetain(string);else memcpy(data,&value,size);return 0;
}
static OSStatus set(AudioServerPlugInDriverRef d,AudioObjectID id,pid_t pid,const AudioObjectPropertyAddress *a,UInt32 qs,const void *q,UInt32 size,const void *data) {
    (void)qs;(void)q;if(d!=driverRef || id!=KORE_DEVICE || a->mSelector!=KORE_LEASE)return kAudioHardwareUnknownPropertyError;
    if(pid<=0 || size!=sizeof(CFBooleanRef) || !data || !*(CFTypeRef const *)data || CFGetTypeID(*(CFTypeRef const *)data)!=CFBooleanGetTypeID())return kAudioHardwareBadPropertySizeError;
    pthread_mutex_lock(&control);OSStatus status=0;
    if(CFBooleanGetValue(*(CFBooleanRef const *)data)) {
        if(consumer && consumer!=pid && (kill(consumer,0)==0 || errno==EPERM))status=kAudioHardwareIllegalOperationError;
        else {consumer=pid;atomic_store(&pcm.read,atomic_load(&pcm.written));}
    } else if(consumer==pid)consumer=0;
    pthread_mutex_unlock(&control);return status;
}
static OSStatus start(AudioServerPlugInDriverRef d,AudioObjectID id,UInt32 clientID) {
    (void)clientID;if(d!=driverRef || id!=KORE_DEVICE)return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&control);if(!atomic_load(&running)){atomic_store(&anchor,AudioGetCurrentHostTime());atomic_fetch_add(&clockSeed,1);}atomic_fetch_add(&running,1);pthread_mutex_unlock(&control);return 0;
}
static OSStatus stop(AudioServerPlugInDriverRef d,AudioObjectID id,UInt32 clientID) {
    (void)clientID;if(d!=driverRef || id!=KORE_DEVICE)return kAudioHardwareBadObjectError;
    pthread_mutex_lock(&control);unsigned n=atomic_load(&running);if(n)atomic_store(&running,n-1);pthread_mutex_unlock(&control);return 0;
}
static OSStatus timestamp(AudioServerPlugInDriverRef d,AudioObjectID id,UInt32 clientID,Float64 *sample,UInt64 *host,UInt64 *seed) {
    (void)clientID;if(d!=driverRef || id!=KORE_DEVICE || !sample || !host || !seed)return kAudioHardwareBadObjectError;
    uint64_t begin=atomic_load(&anchor);uint64_t now=AudioGetCurrentHostTime();
    double frames=begin?AudioConvertHostTimeToNanos(now-begin)*48000./1e9:0;
    uint64_t period=(uint64_t)(frames/KORE_CLOCK_PERIOD);*sample=period*KORE_CLOCK_PERIOD;*host=begin+AudioConvertNanosToHostTime((uint64_t)(period*KORE_CLOCK_PERIOD*1e9/48000));*seed=atomic_load(&clockSeed);return 0;
}
static OSStatus will(AudioServerPlugInDriverRef d,AudioObjectID id,UInt32 clientID,UInt32 operation,Boolean *yes,Boolean *inPlace) {
    (void)clientID;if(d!=driverRef || id!=KORE_DEVICE || !yes || !inPlace)return kAudioHardwareBadObjectError;
    *yes=operation==kAudioServerPlugInIOOperationWriteMix;*inPlace=true;return 0;
}
static OSStatus boundary(AudioServerPlugInDriverRef d,AudioObjectID id,UInt32 clientID,UInt32 op,UInt32 frames,const AudioServerPlugInIOCycleInfo *info) {
    (void)clientID;(void)op;(void)frames;(void)info;return d==driverRef && id==KORE_DEVICE?0:kAudioHardwareBadObjectError;
}
static OSStatus perform(AudioServerPlugInDriverRef d,AudioObjectID id,AudioObjectID stream,UInt32 clientID,UInt32 operation,UInt32 frames,const AudioServerPlugInIOCycleInfo *info,void *main,void *secondary) {
    (void)clientID;(void)info;(void)secondary;
    if(d!=driverRef || id!=KORE_DEVICE || stream!=KORE_STREAM)return kAudioHardwareBadObjectError;
    if(operation!=kAudioServerPlugInIOOperationWriteMix)return 0;
    if(!main || frames>KORE_PCM_CAPACITY)return kAudioHardwareIllegalOperationError;
    kore_pcm_push(&pcm,main,frames);return 0;
}
static AudioServerPlugInDriverInterface driver={NULL,query,add_ref,release_ref,initialize,create,destroy,client,client,config,config,has,settable,get_size,get,set,start,stop,timestamp,will,boundary,perform,boundary};
__attribute__((visibility("default"))) void *KoreAudioFactory(CFAllocatorRef allocator,CFUUIDRef type) {
    (void)allocator;return CFEqual(type,kAudioServerPlugInTypeUUID)?driverRef:NULL;
}
