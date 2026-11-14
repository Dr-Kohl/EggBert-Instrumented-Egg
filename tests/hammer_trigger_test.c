#include "hammer_trigger.h"
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)

static uint32_t clock_us;
static hammer_event_t feed(hammer_trigger_t *h, int16_t x, int16_t y, int16_t z, uint32_t n) {
    hammer_event_t last=HAMMER_NONE;
    while (n--) {
        hammer_event_t event=hammer_sample(h,x,y,z);
        if (event) last=event;
    }
    return last;
}
static hammer_event_t rotate(hammer_trigger_t *h, int16_t x, int16_t y, int16_t z, unsigned n) {
    hammer_event_t last=HAMMER_NONE;
    while (n--) {
        feed(h,0,0,2048,16); // ~240 gyro readings/sec; real FIFO stays independent
        clock_us+=4167;
        hammer_event_t event=hammer_gyro_sample(h,x,y,z,clock_us);
        if (event) last=event;
    }
    return last;
}
static void begin(hammer_trigger_t *h) {
    hammer_reset(h);
    hammer_sample(h,0,0,2048);
    hammer_gyro_sample(h,0,0,0,clock_us);
}
static bool arm(hammer_trigger_t *h) {
    begin(h);
    rotate(h,5143,0,0,240); // ~360 degrees, then back
    rotate(h,-5143,0,0,240);
    rotate(h,0,0,0,500);
    return h->phase==HAMMER_ARMED;
}

int hammer_trigger_test(void) {
    hammer_trigger_t h;
    // Both directions on all three axes, plus a diagonal with normal wobble.
    const int16_t axes[][3]={{5143,0,0},{-5143,0,0},{0,5143,0},
        {0,-5143,0},{0,0,5143},{0,0,-5143},{2970,2970,2970}};
    for (unsigned a=0;a<sizeof axes/sizeof axes[0];++a) {
        begin(&h);
        CHECK(rotate(&h,axes[a][0],axes[a][1],axes[a][2],240)==HAMMER_TURN_ACCEPTED);
        CHECK(h.phase==HAMMER_REVERSE && h.turns==1);
        CHECK(rotate(&h,-axes[a][0],-axes[a][1],-axes[a][2],240)==HAMMER_TURN_ACCEPTED);
        CHECK(h.phase==HAMMER_FINAL_SETTLE && h.turns==2);
        // No fixed gravity reference: mild changing acceleration/orientation
        // and ~24 deg/s hand movement still count as a relaxed pause.
        for (unsigned i=0;i<480;++i) {
            hammer_sample(&h,i%2 ? 300 : -300,500,i%2 ? 1800 : 2400);
            clock_us+=4167;
            hammer_gyro_sample(&h,200,-200,200,clock_us);
        }
        CHECK(h.phase==HAMMER_ARMED);
    }
    // Slower deliberate turns work too, with no special start posture/pause.
    begin(&h); rotate(&h,1286,100,0,800); rotate(&h,-1286,-100,0,800);
    rotate(&h,0,0,0,500); CHECK(h.phase==HAMMER_ARMED);
    // Forward-only wrapping turns cannot arm, even after a pause.
    begin(&h); rotate(&h,0,0,5143,1200); rotate(&h,0,0,0,500);
    CHECK(h.phase<HAMMER_ARMED);
    // A perpendicular turn is not a return turn.
    begin(&h); rotate(&h,5143,0,0,240); rotate(&h,0,-5143,0,240);
    rotate(&h,0,0,0,500); CHECK(h.phase<HAMMER_ARMED);
    // Small back-and-forth handling and translation/tossing don't arm it.
    begin(&h);
    for (unsigned i=0;i<100;++i) {
        rotate(&h,5143,0,0,20); rotate(&h,-5143,0,0,20);
        feed(&h,8000,0,2048,40); feed(&h,0,0,0,100);
    }
    CHECK(h.phase<HAMMER_ARMED && !h.turns);
    // Half-turns don't accumulate across reversals into a full gesture.
    begin(&h);
    for (unsigned i=0;i<10;++i) {
        rotate(&h,5143,0,0,120); rotate(&h,-5143,0,0,120);
    }
    CHECK(!h.turns);
    // Below-deadband drift and an expired partial turn cannot accumulate.
    begin(&h); rotate(&h,100,100,100,3000); CHECK(!h.turns);
    rotate(&h,5143,0,0,100); rotate(&h,0,0,0,1500);
    CHECK(h.phase==HAMMER_TURN && !h.turns);
    rotate(&h,5143,0,0,100); CHECK(!h.turns);
    // Reverse must also finish within six seconds (including idle time).
    begin(&h); rotate(&h,5143,0,0,240); rotate(&h,0,0,0,1500);
    CHECK(h.phase==HAMMER_TURN && !h.turns);
    // A brief bump pauses the final rest rather than erasing progress.
    begin(&h); rotate(&h,5143,0,0,240); rotate(&h,-5143,0,0,240);
    rotate(&h,0,0,0,200); uint32_t quiet=h.quiet_us;
    rotate(&h,1500,0,0,48); CHECK(h.quiet_us==quiet);
    CHECK(rotate(&h,0,0,0,300)==HAMMER_BECAME_ARMED);
    // Sustained turning restarts only the pause, retaining both turns.
    begin(&h); rotate(&h,5143,0,0,240); rotate(&h,-5143,0,0,240);
    rotate(&h,0,0,0,200); rotate(&h,1500,0,0,120);
    CHECK(h.phase==HAMMER_FINAL_SETTLE && h.turns==2 && !h.quiet_us);
    rotate(&h,0,0,0,300); CHECK(h.phase==HAMMER_FINAL_SETTLE);
    rotate(&h,0,0,0,200); CHECK(h.phase==HAMMER_ARMED);
    // Unsupported/freefall readings cannot satisfy rest; expiry resets quietly.
    begin(&h); rotate(&h,5143,0,0,240); rotate(&h,-5143,0,0,240);
    for (unsigned i=0;i<2000;++i) {
        hammer_sample(&h,0,0,0); clock_us+=4167;
        hammer_gyro_sample(&h,0,0,0,clock_us);
    }
    CHECK(h.phase==HAMMER_TURN && !h.turns);
    // Missing readings/time jumps cannot invent a complete turn or pause.
    begin(&h); clock_us+=1000000;
    CHECK(hammer_gyro_sample(&h,32767,0,0,clock_us)==HAMMER_RESET);
    CHECK(!h.turns);
    // Microsecond clock wrap behaves like an ordinary fresh interval.
    clock_us=UINT32_MAX-10000u;
    CHECK(arm(&h));
    // Supported handling, lower impacts and flight alone do not trigger.
    CHECK(arm(&h));
    feed(&h,0,0,2048,HAMMER_MS(1000));
    feed(&h,0,0,14000,HAMMER_MS(100));
    feed(&h,0,0,0,HAMMER_MS(500)); CHECK(h.phase==HAMMER_ARMED);
    // Strict >7 g boundary, either sign and any axis. A single sample suffices.
    for (unsigned axis=0;axis<3;++axis) for (int sign=-1;sign<=1;sign+=2) {
        CHECK(arm(&h)); int16_t v[3]={0};
        v[axis]=(int16_t)(sign*14335);
        CHECK(hammer_sample(&h,v[0],v[1],v[2])==HAMMER_NONE);
        v[axis]=(int16_t)(sign*14336);
        CHECK(hammer_sample(&h,v[0],v[1],v[2])==HAMMER_NONE);
        v[axis]=(int16_t)(sign*14337);
        CHECK(hammer_sample(&h,v[0],v[1],v[2])==HAMMER_TRIGGERED);
        CHECK(h.phase==HAMMER_RECORDING && h.trigger_at==h.seq);
        // No flight after the hit, and later hits cannot retrigger.
        CHECK(feed(&h,0,0,2048,HAMMER_MS(500))==HAMMER_NONE);
        CHECK(hammer_sample(&h,32767,-32768,32767)==HAMMER_NONE);
    }
    // Vector threshold: no individual axis needs to exceed 7 g.
    CHECK(arm(&h)); CHECK(hammer_sample(&h,8192,8192,8192)==HAMMER_NONE);
    CHECK(hammer_sample(&h,8500,-8500,8500)==HAMMER_TRIGGERED);
    CHECK(arm(&h)); CHECK(hammer_sample(&h,-32768,-32768,-32768)==HAMMER_TRIGGERED);
    // Impacts before arming cannot start or prequalify a recording.
    begin(&h);
    for (unsigned phase=HAMMER_TURN;phase<HAMMER_ARMED;++phase) {
        h.phase=(hammer_phase_t)phase;
        CHECK(hammer_sample(&h,32767,0,0)==HAMMER_NONE);
    }
    CHECK(arm(&h)); h.seq=h.armed_at+120u*HAMMER_HZ-2u;
    CHECK(hammer_sample(&h,0,0,2048)==HAMMER_NONE);
    CHECK(hammer_sample(&h,0,0,2048)==HAMMER_TIMED_OUT && !h.turns && h.phase==HAMMER_TURN);
    // Timeout still takes precedence, even at the impact threshold.
    CHECK(arm(&h)); h.seq=h.armed_at+120u*HAMMER_HZ-1u;
    CHECK(hammer_sample(&h,32767,0,0)==HAMMER_TIMED_OUT);
    // An earlier gesture impact cannot make flight alone trigger after arming.
    CHECK(arm(&h)); feed(&h,0,0,0,HAMMER_MS(250)); CHECK(h.phase==HAMMER_ARMED);
    return 0;
}
