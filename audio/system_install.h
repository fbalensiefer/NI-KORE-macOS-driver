/* SPDX-License-Identifier: GPL-2.0-or-later
 * Explicit installation commands. No privilege requests in playback paths.
 */
#include "system_resources.h"
#include <mach-o/dyld.h>
static int kore_task(NSString *path,NSArray<NSString *> *arguments) {
    NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:path];task.arguments=arguments;
    NSError *error=nil;if(![task launchAndReturnError:&error]){report_error("launch helper",error);return 1;}
    [task waitUntilExit];return task.terminationStatus;
}
static NSString *kore_executable_directory(void) {
    uint32_t size=0;_NSGetExecutablePath(NULL,&size);char *buffer=calloc(1,size);
    if(!buffer)return nil;
    NSString *path=nil;if(!_NSGetExecutablePath(buffer,&size))path=[[NSString stringWithUTF8String:buffer] stringByResolvingSymlinksInPath];
    free(buffer);return path.stringByDeletingLastPathComponent;
}
static int kore_system_status(void) {
    AudioDeviceID device=kore_coreaudio_find();
    printf("KORE CoreAudio device=%u installed_bundle=%d\n",device,[[NSFileManager defaultManager] fileExistsAtPath:@"/Library/Audio/Plug-Ins/HAL/KoreAudio.driver"]);
    if(!device)return 1;
    AudioObjectPropertyAddress a={kAudioDevicePropertyNominalSampleRate,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    Float64 rate=0;UInt32 size=sizeof(rate);OSStatus status=AudioObjectGetPropertyData(device,&a,0,NULL,&size,&rate);
    printf("KORE nominal_rate=%.0f query_status=%d\n",rate,status);return status || rate!=48000?1:0;
}
static int kore_system_install(BOOL remove) {
    NSString *directory=kore_executable_directory();if(!directory)return 1;
    if(!remove && ![[NSFileManager defaultManager] fileExistsAtPath:[directory stringByAppendingPathComponent:@"KoreAudio.driver/Contents/Info.plist"]]) {
        fprintf(stderr,"Build the adjacent KoreAudio.driver first (make development-package)\n");return 1;
    }
    char stagingName[]="/private/tmp/KORE1-install-XXXXXX";
    if(!mkdtemp(stagingName))return 1;
    NSString *staging=[NSString stringWithUTF8String:stagingName];
    NSFileManager *fm=[NSFileManager defaultManager];NSError *error=nil;
    if(!remove) {
        for(NSString *item in @[@"KoreAudio.driver",@"kore1-final-bridge"]) {
            if(![fm copyItemAtPath:[directory stringByAppendingPathComponent:item] toPath:[staging stringByAppendingPathComponent:item] error:&error]) {
                report_error("stage install outside protected Documents directory",error);[fm removeItemAtPath:staging error:NULL];return 1;
            }
        }
        if(kore_task(@"/usr/bin/codesign",@[@"--verify",@"--deep",@"--strict",[staging stringByAppendingPathComponent:@"KoreAudio.driver"]])) {[fm removeItemAtPath:staging error:NULL];return 1;}
    }
    NSString *installer=[staging stringByAppendingPathComponent:@"install.sh"];
    if(![[NSString stringWithUTF8String:KORE_INSTALL_SCRIPT] writeToFile:installer atomically:YES encoding:NSUTF8StringEncoding error:&error]) {
        report_error("stage installer",error);[fm removeItemAtPath:staging error:NULL];return 1;
    }
    /* AppleScript quotes every argument; filenames never become shell syntax. */
    NSString *script=@"on run argv\nset command to \"/bin/sh \" & quoted form of (item 1 of argv) & \" \" & quoted form of (item 2 of argv) & \" \" & quoted form of (item 3 of argv)\ndo shell script command with administrator privileges\nend run";
    int rc=kore_task(@"/usr/bin/osascript",@[@"-e",script,@"--",installer,remove?@"uninstall":@"install",staging]);
    [fm removeItemAtPath:staging error:NULL];return rc;
}
/* bootout may return before launchd has removed the service registration.
 * Wait for disappearance before bootstrap to avoid transient EIO/SIGTERMed races. */
static int kore_launchctl_quiet(NSArray<NSString *> *arguments) {
    NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:@"/bin/launchctl"];task.arguments=arguments;
    task.standardOutput=[NSFileHandle fileHandleWithNullDevice];task.standardError=[NSFileHandle fileHandleWithNullDevice];
    NSError *error=nil;if(![task launchAndReturnError:&error])return -1;
    [task waitUntilExit];return task.terminationStatus;
}
static int kore_login_stop(NSString *service) {
    if(kore_launchctl_quiet(@[@"print",service])!=0)return 0;
    (void)kore_launchctl_quiet(@[@"bootout",service]);
    for(unsigned retry=0;retry<50;retry++) {
        if(kore_launchctl_quiet(@[@"print",service])!=0)return 0;
        usleep(100000);
    }
    fprintf(stderr,"Login bridge is still stopping; bootstrap was not attempted\n");return 1;
}
static int kore_login_install(BOOL remove) {
    NSString *label=@"org.nikore.bridge";
    NSString *directory=[NSHomeDirectory() stringByAppendingPathComponent:@"Library/LaunchAgents"];
    NSString *path=[directory stringByAppendingPathComponent:[label stringByAppendingString:@".plist"]];
    NSString *domain=[NSString stringWithFormat:@"gui/%u",getuid()];
    NSString *service=[domain stringByAppendingFormat:@"/%@",label];
    if(remove) {
        if(kore_login_stop(service))return 1;
        if([[NSFileManager defaultManager] fileExistsAtPath:path]) {
            NSError *error=nil;if(![[NSFileManager defaultManager] removeItemAtPath:path error:&error]){report_error("remove login agent",error);return 1;}
        }
        return 0;
    }
    NSString *binary=@"/Library/Application Support/KORE1/kore1-final-bridge";
    if(![[NSFileManager defaultManager] isExecutableFileAtPath:binary])return 1;
    NSError *error=nil;
    if(![[NSFileManager defaultManager] createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:&error]){report_error("login directory",error);return 1;}
    NSDictionary *agent=@{@"Label":label,@"ProgramArguments":@[binary,@"--run",@"--coreaudio-pcm",@"--continuous",@"--mixxx-feedback"],
        @"RunAtLoad":@YES,@"KeepAlive":@YES,@"ThrottleInterval":@10,@"ProcessType":@"Interactive",
        @"StandardOutPath":[NSHomeDirectory() stringByAppendingPathComponent:@"Library/Logs/KORE1-bridge.log"],
        @"StandardErrorPath":[NSHomeDirectory() stringByAppendingPathComponent:@"Library/Logs/KORE1-bridge-error.log"]};
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:agent format:NSPropertyListXMLFormat_v1_0 options:0 error:&error];
    if(!data || ![data writeToFile:path options:NSDataWritingAtomic error:&error]){report_error("write login agent",error);return 1;}
    if(kore_login_stop(service))return 1;
    return kore_task(@"/bin/launchctl",@[@"bootstrap",domain,path]);
}
