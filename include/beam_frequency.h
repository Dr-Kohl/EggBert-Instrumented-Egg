#pragma once
#include <math.h>
#include <stddef.h>

// Autocorrelation estimate over 0.3--20 Hz. Input is averaged to 60 Hz;
// remove the DC offset and linear drift before comparing repeated cycles.
// Return zero when no convincing periodic signal is present.
static inline float beam_frequency(float *signal, size_t count) {
    if (count < 240u) return 0;
    float mean = 0, slope_sum = 0, slope_den = 0;
    float middle = (float)(count-1u)/2;
    for (size_t i=0; i<count; ++i) mean += signal[i]/count;
    for (size_t i=0; i<count; ++i) {
        float t = i-middle;
        slope_sum += t*(signal[i]-mean); slope_den += t*t;
    }
    float energy = 0;
    for (size_t i=0; i<count; ++i) {
        signal[i] -= mean + (slope_sum/slope_den)*(i-middle);
        energy += signal[i]*signal[i];
    }
    if (energy/count < 80.0f*80.0f) return 0; // below about 0.005 g RMS
    float corr[202] = {0};
    for (unsigned lag=2; lag<=201; ++lag) {
        float cross=0, e1=0, e2=0;
        for (size_t i=0; i+lag<count; ++i) {
            float a=signal[i], b=signal[i+lag];
            cross+=a*b; e1+=a*a; e2+=b*b;
        }
        corr[lag] = e1>0 && e2>0 ? cross/sqrtf(e1*e2) : 0;
    }
    float best = 0;
    for (unsigned lag=3; lag<=200; ++lag)
        if (corr[lag]>corr[lag-1] && corr[lag]>=corr[lag+1] && corr[lag]>best) best=corr[lag];
    if (best < 0.65f) return 0;
    for (unsigned lag=3; lag<=200; ++lag) {
        if (corr[lag]>corr[lag-1] && corr[lag]>=corr[lag+1] && corr[lag]>=best*0.90f) {
            float curvature = corr[lag-1]-2*corr[lag]+corr[lag+1];
            float delta = curvature ? 0.5f*(corr[lag-1]-corr[lag+1])/curvature : 0;
            return 60.0f/(lag+delta);
        }
    }
    return 0;
}
