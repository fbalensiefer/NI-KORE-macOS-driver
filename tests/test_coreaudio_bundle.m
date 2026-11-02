/* Load the actual local signed bundle without registering it with CoreAudio. */
#import <Foundation/Foundation.h>
#import <CoreAudio/AudioServerPlugIn.h>
#include <assert.h>
int main(void) {@autoreleasepool {
    NSURL *url=[NSURL fileURLWithPath:[[NSFileManager defaultManager].currentDirectoryPath stringByAppendingPathComponent:@"build/KoreAudio.driver"]];
    CFPlugInRef plugin=CFPlugInCreate(NULL,(__bridge CFURLRef)url);assert(plugin);
    CFArrayRef factories=CFPlugInFindFactoriesForPlugInTypeInPlugIn(kAudioServerPlugInTypeUUID,plugin);
    assert(factories && CFArrayGetCount(factories)==1);
    AudioServerPlugInDriverRef driver=CFPlugInInstanceCreate(NULL,CFArrayGetValueAtIndex(factories,0),kAudioServerPlugInTypeUUID);assert(driver);
    AudioObjectPropertyAddress address={kAudioDevicePropertyDeviceUID,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    CFStringRef uid=NULL;UInt32 used=0;
    assert(!(*driver)->GetPropertyData(driver,2,getpid(),&address,0,NULL,sizeof(uid),&used,&uid));
    assert(CFEqual(uid,CFSTR("org.nikore.experimental.output")));CFRelease(uid);
    (*driver)->Release(driver);CFRelease(factories);CFRelease(plugin);
    puts("PASS actual CoreAudio bundle: factory registration, ABI and device UID; not installed");
}}
