/* SPDX-License-Identifier: GPL-2.0-or-later */
#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
static inline BOOL caiaq_native_identity(NSDictionary *properties) {
    for(NSString *key in @[@"idVendor",@"idProduct",@"bInterfaceNumber"])
        if(![properties[key] isKindOfClass:[NSNumber class]])return NO;
    return [properties[@"idVendor"] unsignedIntValue]==0x17cc &&
           [properties[@"idProduct"] unsignedIntValue]==0x4711 &&
           [properties[@"bInterfaceNumber"] unsignedIntValue]==0;
}
/* Read-only enumeration. Caller releases the returned registry reference. */
static inline io_service_t caiaq_native_find_interface(void) {
    io_iterator_t iterator=IO_OBJECT_NULL;
    kern_return_t kr=IOServiceGetMatchingServices(kIOMainPortDefault,IOServiceMatching("IOUSBHostInterface"),&iterator);
    if(kr!=KERN_SUCCESS) {fprintf(stderr,"NATIVE interface enumeration failed: 0x%08x\n",kr);return IO_OBJECT_NULL;}
    io_service_t selected=IO_OBJECT_NULL,entry;unsigned matches=0;
    while((entry=IOIteratorNext(iterator))) {
        CFMutableDictionaryRef raw=NULL;
        kr=IORegistryEntryCreateCFProperties(entry,&raw,kCFAllocatorDefault,0);
        NSDictionary *properties=CFBridgingRelease(raw);
        if(kr==KERN_SUCCESS && caiaq_native_identity(properties)) {
            matches++;
            if(!selected) {selected=entry;entry=IO_OBJECT_NULL;}
        }
        if(entry)IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);
    if(matches!=1) {
        fprintf(stderr,"NATIVE discovery: matching KORE1 interface-0 services=%u; need exactly one\n",matches);
        if(selected)IOObjectRelease(selected);
        return IO_OBJECT_NULL;
    }
    return selected;
}
/* UsbLinkSpeed is a bit rate, avoiding incompatible USBSpeed/Device Speed enums. */
static inline BOOL caiaq_native_high_speed(io_service_t service) {
    id speed=CFBridgingRelease(IORegistryEntrySearchCFProperty(service,kIOServicePlane,
        CFSTR("UsbLinkSpeed"),kCFAllocatorDefault,kIORegistryIterateRecursively|kIORegistryIterateParents));
    printf("NATIVE registry: KORE1 interface=0 UsbLinkSpeed=%llu bit/s\n",
           [speed isKindOfClass:[NSNumber class]]?[speed unsignedLongLongValue]:0ULL);
    return [speed isKindOfClass:[NSNumber class]] && [speed unsignedLongLongValue]==480000000ULL;
}
