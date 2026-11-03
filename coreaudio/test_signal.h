/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded test signals only. Final USB PCM gain remains independently capped.
 */
#ifndef KORE_TEST_SIGNAL_H
#define KORE_TEST_SIGNAL_H
#include <stdint.h>
#include <math.h>
#define KORE_STEREO_TEST_FRAMES 240000u
static inline float kore_test_tone(uint64_t index,unsigned frequency) {
    double gain=1;
    if(index<240)gain=index/240.;
    if(index>=47760)gain=(47999-index)/240.;
    return (float)(gain*sin(6.283185307179586*frequency*(index%2400)/48000.));
}
static inline void kore_test_signal(uint64_t index,int stereo,float values[2]) {
    values[0]=values[1]=0;
    if(index>=24000 && index<72000)values[0]=kore_test_tone(index-24000,440);
    if(stereo && index>=96000 && index<144000)values[1]=kore_test_tone(index-96000,660);
    if(stereo && index>=168000 && index<216000) {
        values[0]=kore_test_tone(index-168000,440);
        values[1]=kore_test_tone(index-168000,660);
    }
}
/* 50ms Hann-windowed chirp for detecting delayed copies in the analog path. */
static inline float kore_echo_pulse(uint64_t index) {
    if(index>=2400)return 0;
    double t=index/48000.,duration=.05,slope=(10000.-200.)/duration;
    double window=.5-.5*cos(6.283185307179586*index/2399.);
    return (float)(window*sin(6.283185307179586*(200*t+.5*slope*t*t)));
}
static inline void kore_echo_signal(uint64_t index,float values[2]) {
    values[0]=index>=24000?kore_echo_pulse(index-24000):0;
    values[1]=index>=72000?kore_echo_pulse(index-72000):0;
}
#endif
