/* Capture the system default input device, expected BlackHole 2ch.\n * Writes 48kHz stereo float32 PCM to FIFO consumed by KORE bridge. */
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
static volatile sig_atomic_t active=1;
static int pipe_fd=-1;
static AudioQueueRef queue;
static void stop(int sig){(void)sig;active=0;}
static void callback(void *ud, AudioQueueRef q, AudioQueueBufferRef b, const AudioTimeStamp *ts, UInt32 count,const AudioStreamPacketDescription *pd){
 (void)ud;(void)ts;(void)count;(void)pd;
 if(pipe_fd>=0 && b->mAudioDataByteSize){
   ssize_t n=write(pipe_fd,b->mAudioData,b->mAudioDataByteSize);
   (void)n; /* Nonblocking: drop on overflow rather than stall CoreAudio. */
 }
 if(active) AudioQueueEnqueueBuffer(q,b,0,NULL);
}
int main(void){
 signal(SIGINT,stop);signal(SIGTERM,stop);
 if(mkfifo("/tmp/kore1-pcm.fifo",0600)<0 && errno!=EEXIST){perror("mkfifo");return 1;}
 AudioStreamBasicDescription fmt={0};
 fmt.mSampleRate=48000;fmt.mFormatID=kAudioFormatLinearPCM;
 fmt.mFormatFlags=kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked|kAudioFormatFlagsNativeEndian;
 fmt.mChannelsPerFrame=2;fmt.mBitsPerChannel=32;fmt.mFramesPerPacket=1;
 fmt.mBytesPerFrame=8;fmt.mBytesPerPacket=8;
 OSStatus err=AudioQueueNewInput(&fmt,callback,NULL,CFRunLoopGetCurrent(),kCFRunLoopCommonModes,0,&queue);
 if(err){fprintf(stderr,"AudioQueueNewInput failed: %d. Select BlackHole 2ch as macOS default input.\\n",(int)err);return 1;}
 for(int i=0;i<4;i++){AudioQueueBufferRef b;err=AudioQueueAllocateBuffer(queue,8192,&b);if(err)break;err=AudioQueueEnqueueBuffer(queue,b,0,NULL);if(err)break;}
 if(err){fprintf(stderr,"AudioQueue buffers failed: %d\\n",(int)err);AudioQueueDispose(queue,true);return 1;}
 pipe_fd=open("/tmp/kore1-pcm.fifo",O_WRONLY|O_NONBLOCK);
 if(pipe_fd<0){perror("open FIFO (start bridge first)");AudioQueueDispose(queue,true);return 1;}
 err=AudioQueueStart(queue,NULL);
 if(err){fprintf(stderr,"AudioQueueStart failed: %d\\n",(int)err);return 1;}
 puts("Capturing default input at 48kHz stereo float32. CTRL+C to stop.");
 while(active) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.1,false);
 AudioQueueStop(queue,true);AudioQueueDispose(queue,true);close(pipe_fd);return 0;
}
