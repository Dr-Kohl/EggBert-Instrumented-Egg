#include "hammer_trigger.h"

static hammer_event_t feed(hammer_trigger_t *h, int16_t x, int16_t y, int16_t z, uint32_t n) {
    hammer_event_t last=HAMMER_NONE;
    while (n--) {
        hammer_event_t event=hammer_sample(h,x,y,z);
        if (event) last=event;
    }
    return last;
}
static void toss(hammer_trigger_t *h) {
    feed(h,0,0,4096,HAMMER_MS(30));
    feed(h,0,0,0,HAMMER_MS(250));
    feed(h,0,0,6000,HAMMER_MS(30));
    feed(h,0,0,2048,HAMMER_MS(650));
}
static bool arm(hammer_trigger_t *h) {
    hammer_reset(h);
    feed(h,0,0,2048,HAMMER_MS(600));
    for (unsigned i=0;i<3;++i) toss(h);
    if (h->tosses!=3 || h->phase!=HAMMER_FINAL_SETTLE) return false;
    feed(h,0,0,2048,HAMMER_MS(2100));
    return h->phase==HAMMER_ARMED;
}

int hammer_trigger_test(void) {
    hammer_trigger_t h;
    hammer_reset(&h);
    // Vigorous shaking, including brief near-zero intervals, must not arm.
    for (unsigned i=0;i<120;++i) {
        feed(&h,0,0,2048,HAMMER_MS(600));
        feed(&h,8000,0,2048,HAMMER_MS(30));
        feed(&h,0,0,0,HAMMER_MS(40));
        feed(&h,-8000,0,2048,HAMMER_MS(30));
    }
    if (h.phase>=HAMMER_ARMED || h.tosses) return 1;
    // Dropping without a preceding push, or very long freefall, resets.
    hammer_reset(&h); feed(&h,0,0,2048,HAMMER_MS(600));
    feed(&h,0,0,0,HAMMER_MS(250));
    if (h.tosses || h.phase!=HAMMER_SETTLE) return 2;
    hammer_reset(&h); feed(&h,0,0,2048,HAMMER_MS(600));
    feed(&h,0,0,4096,HAMMER_MS(30)); feed(&h,0,0,0,HAMMER_MS(1000));
    if (h.tosses || h.phase!=HAMMER_SETTLE) return 3;
    // Three valid tosses still require the final quiet interval.
    if (!arm(&h)) return 4;
    // An impact and 100 ms of flight is rejected; stay armed.
    feed(&h,0,0,16000,HAMMER_MS(10));
    feed(&h,0,0,0,HAMMER_MS(100));
    feed(&h,0,0,2048,HAMMER_MS(30));
    if (h.phase!=HAMMER_ARMED) return 5;
    // Sustained flight confirms only after the full 200 ms.
    feed(&h,0,0,16000,HAMMER_MS(10));
    feed(&h,0,0,0,HAMMER_MS(200)-1u);
    if (h.phase!=HAMMER_LAUNCH) return 6;
    if (hammer_sample(&h,0,0,0)!=HAMMER_TRIGGERED || h.phase!=HAMMER_RECORDING) return 7;
    if (h.launch_at-h.trigger_at!=1u || h.seq-h.trigger_at!=HAMMER_MS(200)) return 8;
    // Small interruptions tolerated; sustained acceleration isn't freefall.
    if (!arm(&h)) return 9;
    feed(&h,0,0,5000,1);
    feed(&h,0,0,0,HAMMER_MS(100));
    feed(&h,0,0,2048,10);
    if (feed(&h,0,0,0,HAMMER_MS(100))!=HAMMER_TRIGGERED) return 10;
    if (!arm(&h)) return 11;
    feed(&h,0,0,5000,1); feed(&h,0,0,0,HAMMER_MS(100));
    feed(&h,0,0,2048,HAMMER_MS(6));
    if (h.phase!=HAMMER_ARMED) return 12;
    // Timeout expires at two minutes, including a pending launch candidate.
    if (!arm(&h)) return 13;
    h.seq=h.armed_at+120u*HAMMER_HZ-2u;
    if (hammer_sample(&h,0,0,2048)!=HAMMER_NONE) return 14;
    if (hammer_sample(&h,0,0,2048)!=HAMMER_TIMED_OUT || h.tosses || h.phase!=HAMMER_SETTLE) return 15;
    // Partial sequences cannot persist indefinitely.
    hammer_reset(&h); feed(&h,0,0,2048,HAMMER_MS(600)); toss(&h);
    feed(&h,0,0,2048,16u*HAMMER_HZ);
    if (h.tosses || h.phase>=HAMMER_ARMED) return 16;
    // A roughly one-foot toss lasts ~500 ms. Modest push, noisy flight and
    // soft support-only catch must count, without requiring a hard impact.
    hammer_reset(&h); feed(&h,0,0,2048,HAMMER_MS(600));
    for (unsigned i=0;i<3;++i) {
        feed(&h,0,0,2700,HAMMER_MS(40)); // only 1.32 g
        for (unsigned j=0;j<5;++j) {
            feed(&h,300,100,200,HAMMER_MS(99));
            feed(&h,0,0,1500,HAMMER_MS(1)); // brief interruption
        }
        feed(&h,0,0,2200,HAMMER_MS(100)); // no >1.5 g catch
        feed(&h,0,0,2048,HAMMER_MS(650));
        if (h.tosses!=i+1) return 17+i;
    }
    feed(&h,0,0,2048,HAMMER_MS(2100));
    if (h.phase!=HAMMER_ARMED) return 20;
    return 0;
}
