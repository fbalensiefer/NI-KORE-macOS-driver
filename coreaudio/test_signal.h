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
#endif
