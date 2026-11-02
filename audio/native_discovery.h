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
/* Freshly connected vendor-class KOREs may have no selected configuration, so no
 * interface service exists until SET_CONFIGURATION(1). Never reset a configured
 * device or seize another interface owner. Called only in explicit hardware mode. */
static inline BOOL caiaq_native_configure_if_needed(dispatch_queue_t queue) {
    io_iterator_t iterator=IO_OBJECT_NULL;
    if(IOServiceGetMatchingServices(kIOMainPortDefault,IOServiceMatching("IOUSBHostDevice"),&iterator))return NO;
    io_service_t selected=0,entry;unsigned matches=0;
    while((entry=IOIteratorNext(iterator))) {
        CFMutableDictionaryRef raw=NULL;IORegistryEntryCreateCFProperties(entry,&raw,kCFAllocatorDefault,0);
        NSDictionary *p=CFBridgingRelease(raw);
        if([p[@"idVendor"] isKindOfClass:[NSNumber class]] && [p[@"idProduct"] isKindOfClass:[NSNumber class]] &&
           [p[@"idVendor"] unsignedIntValue]==0x17cc && [p[@"idProduct"] unsignedIntValue]==0x4711) {
            matches++;if(!selected){selected=entry;entry=0;}
        }
        if(entry)IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);
    if(matches!=1){if(selected)IOObjectRelease(selected);return NO;}
    CFTypeRef configured=IORegistryEntryCreateCFProperty(selected,CFSTR("kUSBCurrentConfiguration"),kCFAllocatorDefault,0);
    BOOL active=configured && CFGetTypeID(configured)==CFNumberGetTypeID() && [(__bridge NSNumber *)configured unsignedIntValue]>0;
    if(configured)CFRelease(configured);
    if(active){IOObjectRelease(selected);return YES;}
    NSError *error=nil;
    IOUSBHostDevice *device=[[IOUSBHostDevice alloc] initWithIOService:selected options:IOUSBHostObjectInitOptionsNone queue:queue error:&error interestHandler:nil];
    IOObjectRelease(selected);
    BOOL success=device && [device configureWithValue:1 matchInterfaces:YES error:&error];
    if(!success)fprintf(stderr,"NATIVE select configuration 1: %s\n",error.localizedDescription.UTF8String);
    else puts("NATIVE selected previously unset USB configuration 1");
    [device destroy];return success;
}
