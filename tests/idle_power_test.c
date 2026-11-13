#include <assert.h>
#include <stdint.h>
#include "idle_power.h"
int idle_power_test(void) {
    idle_power_t idle;
    idle_power_init(&idle,1000);
    assert(!idle_power_due(&idle,60999,false));
    assert(idle_power_due(&idle,61000,false));
    // Static gravity plus sub-threshold noise must not keep the device awake.
    idle_power_init(&idle,0);
    for(uint32_t t=0;t<=60000;t+=250) {
        int16_t xyz[3]={(int16_t)(t%13),0,16384};
        idle_power_motion(&idle,xyz,t);
    }
    assert(idle_power_due(&idle,60000,false));
    // Slow motion accumulates relative to the anchor, rather than adjacent reads.
    for(unsigned i=0;i<20;i++) {
        int16_t xyz[3]={(int16_t)(100*i),0,16384};
        idle_power_motion(&idle,xyz,60000+i*100);
    }
    assert(!idle_power_due(&idle,62000,false));
    assert(!idle_power_due(&idle,999999,true));
    assert(!idle_power_due(&idle,1059998,false));
    assert(idle_power_due(&idle,1059999,false));
    idle_power_init(&idle,UINT32_MAX-30000u);
    assert(!idle_power_due(&idle,29998,false));
    assert(idle_power_due(&idle,29999,false));
    return 0;
}
