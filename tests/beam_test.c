#include "beam_trigger.h"
#include "beam_frequency.h"

// Returns the failing check number; zero means all tests passed.
int beam_test(void) {
#ifdef BEAM_CAPTURE_TEST
    extern int beam_capture_test(void);
    int capture_result=beam_capture_test();
    if (capture_result) return capture_result;
#endif
    beam_trigger_t s;
    beam_trigger_reset(&s);
    // Tilted resting gravity: do not assume any particular mounting direction.
    for (unsigned i=0; i<480; ++i)
        if (beam_trigger_sample(&s,4915,0,6554)) return 1;
    if (!s.ready) return 2;
    for (unsigned i=0; i<100; ++i)
        if (beam_trigger_sample(&s,5415,0,6554)) return 3;
    // Four strong samples, followed by one below threshold, must not trigger.
    for (unsigned i=0; i<4; ++i)
        if (beam_trigger_sample(&s,7000,0,6554)) return 4;
    if (beam_trigger_sample(&s,4915,0,6554)) return 5;
    // The fifth consecutive strong sample must trigger.
    for (unsigned i=0; i<5; ++i)
        if (beam_trigger_sample(&s,7000,0,6554) != (i==4)) return 6;
    beam_trigger_reset(&s);
    for (unsigned i=0; i<479; ++i) beam_trigger_sample(&s,0,0,8192);
    beam_trigger_sample(&s,1500,0,8192);
    if (s.ready || s.count) return 7;
    for (unsigned i=0; i<480; ++i) beam_trigger_sample(&s,0,0,8192);
    if (!s.ready) return 8;
    // Multi-axis vector change, even when neither individual axis exceeds 0.2g.
    for (unsigned i=0; i<5; ++i)
        if (beam_trigger_sample(&s,1250,1250,8192) != (i==4)) return 9;

    static float signal[840];
    const float frequencies[]={0.6f,0.8f,1.3f,3.5f,5.0f,10.0f};
    for (unsigned f=0; f<6; ++f) {
        for (unsigned i=0; i<840; ++i) {
            float t=i/60.0f;
            signal[i]=6000+15*t+900*expf(-t/12)*sinf(6.2831853f*frequencies[f]*t)
                      +20*sinf(2.37f*i);
        }
        float estimated=beam_frequency(signal,840);
        if (fabsf(estimated-frequencies[f]) > frequencies[f]*0.025f) return 10+f;
    }
    for (unsigned i=0; i<840; ++i) signal[i]=8192;
    if (beam_frequency(signal,840)!=0) return 20;
    // Deterministic broad noise must not yield a plausible frequency.
    uint32_t random=1;
    for (unsigned i=0; i<840; ++i) {
        random=random*1664525u+1013904223u;
        signal[i]=(int32_t)(random>>16)-32768;
    }
    if (beam_frequency(signal,840)!=0) return 21;
    return 0;
}
