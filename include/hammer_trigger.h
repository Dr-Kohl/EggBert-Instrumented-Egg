#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

// Accelerometer: capture's 3840 Hz clock, 2048 counts/g. Fresh gyro: elapsed
// wall time, 70 mdps/count. No heap, hardware calls, or recording arrays here.
#define HAMMER_HZ 3840u
#define HAMMER_MS(ms) ((HAMMER_HZ * (ms)) / 1000u)
#define HAMMER_TURN_DEGREES 270.0f
#define HAMMER_TURN_TIMEOUT_US 6000000u
#define HAMMER_PAUSE_TIMEOUT_US 8000000u
typedef enum {
    HAMMER_TURN, HAMMER_REVERSE, HAMMER_FINAL_SETTLE,
    HAMMER_ARMED, HAMMER_LAUNCH, HAMMER_RECORDING
} hammer_phase_t;
typedef enum {
    HAMMER_NONE, HAMMER_RESET, HAMMER_TURN_ACCEPTED, HAMMER_BECAME_ARMED,
    HAMMER_TRIGGERED, HAMMER_TIMED_OUT
} hammer_event_t;
typedef struct {
    hammer_phase_t phase;
    uint32_t seq, phase_at, armed_at, flight, gap, gaps;
    uint32_t impulse_at, launch_at, trigger_at;
    uint32_t gyro_at_us, gesture_us, quiet_us, quiet_gap_us;
    float turn[3], direction[3], reverse_degrees, travel_degrees;
    uint8_t turns;
    bool impulse_valid, gyro_valid, supported;
} hammer_trigger_t;

static inline uint64_t hammer_mag2(int32_t x, int32_t y, int32_t z) {
    return (uint64_t)((int64_t)x*x + (int64_t)y*y + (int64_t)z*z);
}
static inline void hammer_reset(hammer_trigger_t *h) {
    *h = (hammer_trigger_t){0};
}
static inline void hammer_phase(hammer_trigger_t *h, hammer_phase_t phase) {
    h->phase = phase; h->phase_at = h->seq;
    h->flight = h->gap = h->gaps = 0;
}
static inline hammer_event_t hammer_restart(hammer_trigger_t *h, hammer_event_t event) {
    uint32_t seq=h->seq;
    bool supported=h->supported;
    hammer_reset(h); h->seq=seq; h->supported=supported;
    return event;
}

// Any starting axis/sign works, including a diagonal. The return turn must
// reverse the first turn's measured direction. Small cross-axis wobble is OK.
static inline hammer_event_t hammer_gyro_sample(hammer_trigger_t *h,
        int16_t x, int16_t y, int16_t z, uint32_t now_us) {
    if (h->phase >= HAMMER_ARMED) return HAMMER_NONE;
    uint32_t dt=now_us-h->gyro_at_us;
    bool valid=h->gyro_valid;
    h->gyro_at_us=now_us; h->gyro_valid=true;
    if (!valid) return HAMMER_NONE;
    // Never turn a scheduling gap into invented rotation or rest.
    if (!dt) return HAMMER_NONE;
    if (dt>50000u) return hammer_restart(h,HAMMER_RESET);
    float rate[3]={x*0.07f,y*0.07f,z*0.07f};
    float speed=sqrtf(rate[0]*rate[0]+rate[1]*rate[1]+rate[2]*rate[2]);
    if (h->phase==HAMMER_FINAL_SETTLE) {
        h->gesture_us+=dt;
        // Relaxed hand-held pause: no fixed gravity vector or orientation.
        // Small tremors/slow repositioning and <=350 ms bumps keep progress.
        if (speed<=45.0f && h->supported) {
            h->quiet_us+=dt; h->quiet_gap_us=0;
        } else {
            h->quiet_gap_us+=dt;
            if (h->quiet_gap_us>350000u) h->quiet_us=0;
        }
        if (h->quiet_us>=2000000u) {
            hammer_phase(h,HAMMER_ARMED); h->armed_at=h->seq;
            h->impulse_valid=false;
            return HAMMER_BECAME_ARMED;
        }
        if (h->gesture_us>HAMMER_PAUSE_TIMEOUT_US)
            return hammer_restart(h,HAMMER_RESET);
        return HAMMER_NONE;
    }
    if (speed>=20.0f || h->gesture_us) h->gesture_us+=dt;
    if (h->gesture_us>HAMMER_TURN_TIMEOUT_US)
        return hammer_restart(h,HAMMER_RESET);
    if (speed<20.0f) return HAMMER_NONE; // don't integrate drift
    float seconds=dt*0.000001f;
    if (h->phase==HAMMER_TURN) {
        for (unsigned a=0;a<3;++a) h->turn[a]+=rate[a]*seconds;
        h->travel_degrees+=speed*seconds;
        float angle=sqrtf(h->turn[0]*h->turn[0]+h->turn[1]*h->turn[1]+h->turn[2]*h->turn[2]);
        if (angle>=HAMMER_TURN_DEGREES && angle>=h->travel_degrees*0.65f) {
            for (unsigned a=0;a<3;++a) h->direction[a]=h->turn[a]/angle;
            h->turns=1; h->gesture_us=1;
            hammer_phase(h,HAMMER_REVERSE);
            return HAMMER_TURN_ACCEPTED;
        }
    } else if (h->phase==HAMMER_REVERSE) {
        float along=rate[0]*h->direction[0]+rate[1]*h->direction[1]+rate[2]*h->direction[2];
        h->reverse_degrees-=along*seconds;
        if (h->reverse_degrees<0) h->reverse_degrees=0;
        if (h->reverse_degrees>=HAMMER_TURN_DEGREES) {
            h->turns=2; h->gesture_us=0;
            hammer_phase(h,HAMMER_FINAL_SETTLE);
            return HAMMER_TURN_ACCEPTED;
        }
    }
    return HAMMER_NONE;
}

// Launch detection/window timing remain on the acceleration FIFO sample clock.
static inline hammer_event_t hammer_sample(hammer_trigger_t *h, int16_t x, int16_t y, int16_t z) {
    ++h->seq;
    uint64_t m=hammer_mag2(x,y,z);
    h->supported=m>=1331ull*1331u && m<=2867ull*2867u; // 0.65--1.4 g
    bool freefall=m < 922ull*922u;
    bool impulse=m > 3072ull*3072u;
    if (impulse && h->phase != HAMMER_LAUNCH) {
        h->impulse_at=h->seq; h->impulse_valid=true;
    }
    if (h->phase == HAMMER_RECORDING) return HAMMER_NONE;
    if ((h->phase == HAMMER_ARMED || h->phase == HAMMER_LAUNCH) &&
        h->seq-h->armed_at >= HAMMER_HZ*120u)
        return hammer_restart(h,HAMMER_TIMED_OUT);
    switch (h->phase) {
    case HAMMER_ARMED:
        if (freefall && h->impulse_valid && h->seq-h->impulse_at <= HAMMER_MS(150)) {
            h->trigger_at=h->impulse_at; h->launch_at=h->seq;
            hammer_phase(h,HAMMER_LAUNCH); h->flight=1;
        }
        break;
    case HAMMER_LAUNCH:
        if (freefall) { ++h->flight; h->gap=0; }
        else { ++h->gap; ++h->gaps; }
        if (h->gap > HAMMER_MS(5) || h->gaps > HAMMER_MS(10)) {
            hammer_phase(h,HAMMER_ARMED); h->impulse_valid=false;
        } else if (h->flight >= HAMMER_MS(200)) {
            hammer_phase(h,HAMMER_RECORDING);
            return HAMMER_TRIGGERED;
        }
        break;
    default: break;
    }
    return HAMMER_NONE;
}
