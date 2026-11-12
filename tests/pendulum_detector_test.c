#include <assert.h>
#include <math.h>
#include <stdint.h>
#include "pendulum_detector.h"

static pendulum_detector_t replay(float period, unsigned mode, float phase, uint32_t origin) {
    pendulum_detector_t d;
    pendulum_detector_reset(&d);
    for(uint32_t ms=0;ms<70000 && !d.complete && !d.weak;) {
        float t=ms/1000.0f, amplitude=mode==4 ? 0.003f : mode==5 ? 0.0f : 0.20f;
        if(mode==2) amplitude*=expf(-0.18f*t);
        float motion=t+phase;
        if(mode==6) motion+=0.14f*sinf(2.7f*t);
        float signal=1.0f+amplitude*cosf(12.5663706f*motion/period);
        if(mode==1 || mode==2 || mode==4) signal+=0.004f*sinf(106.81415f*t);
        if(mode==3 && t>2.1f && t<2.5f) signal=1.0f; // missed crossing
        pendulum_detector_sample(&d,signal,origin+ms);
        ms+=mode==7 ? (ms%3==0 ? 35u : 15u) : 20u;
    }
    return d;
}
int pendulum_detector_test(void) {
    const float periods[]={0.8f,1.2f,1.4f,2.0f,4.0f};
    for(unsigned p=0;p<5;p++) for(unsigned phase=0;phase<3;phase++) {
        pendulum_detector_t d=replay(periods[p],0,phase*0.17f,1000);
        assert(d.complete && !d.weak);
        assert(fabsf(d.period_seconds-periods[p])<0.035f);
    }
    for(unsigned mode=1;mode<=7;mode++) {
        pendulum_detector_t d=replay(1.4f,mode,0,1000);
        if(mode==4 || mode==5 || mode==6) assert(!d.complete && d.weak);
        else { assert(d.complete); assert(fabsf(d.period_seconds-1.4f)<0.045f); }
    }
    pendulum_detector_t d=replay(2.0f,7,0,UINT32_MAX-3000u);
    assert(d.complete && fabsf(d.period_seconds-2.0f)<0.04f);
    pendulum_detector_reset(&d);
    assert(!d.complete && !d.weak && !d.have_peak && d.interval_count==0);
    return 0;
}
