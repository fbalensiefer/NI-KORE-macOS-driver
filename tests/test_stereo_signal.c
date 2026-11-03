#include "../coreaudio/test_signal.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    unsigned crossings[2]={0},active[2]={0};float last[2]={0};double power[2]={0};
    for(unsigned n=0;n<KORE_STEREO_TEST_FRAMES+512;n++) {
        float frame[2];kore_test_signal(n,1,frame);
        int left=(n>=24000 && n<72000)||(n>=168000 && n<216000);
        int right=(n>=96000 && n<144000)||(n>=168000 && n<216000);
        for(unsigned ch=0;ch<2;ch++) {
            assert(isfinite(frame[ch]) && fabsf(frame[ch])<=1);
            assert((ch?right:left) || frame[ch]==0);
            if(ch?right:left){if(last[ch]<=0 && frame[ch]>0)crossings[ch]++;active[ch]++;power[ch]+=frame[ch]*frame[ch];}
            last[ch]=frame[ch];
        }
    }
    assert(active[0]==96000 && active[1]==96000);
    assert(crossings[0]>=878 && crossings[0]<=882 && crossings[1]>=1318 && crossings[1]<=1322);
    for(unsigned ch=0;ch<2;ch++)assert(sqrt(power[ch]/active[ch])>.70 && sqrt(power[ch]/active[ch])<.71);
    float frame[2];kore_test_signal(100000,0,frame);assert(frame[0]==0 && frame[1]==0);
    for(unsigned n=0;n<120000;n++) {
        float pulse[2];kore_echo_signal(n,pulse);
        assert(isfinite(pulse[0]) && fabsf(pulse[0])<=1 && isfinite(pulse[1]) && fabsf(pulse[1])<=1);
        assert((n>=24000 && n<26400) || pulse[0]==0);
        assert((n>=72000 && n<74400) || pulse[1]==0);
    }
    puts("PASS bounded stereo signal: independent 440/660Hz channels, left/right/dual windows, fades and silence");
}
