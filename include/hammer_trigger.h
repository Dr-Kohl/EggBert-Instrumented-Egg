#pragma once
#include <stdbool.h>
#include <stdint.h>

// Sample-clock gesture recognizer; all inputs are calibrated low-g XYZ at
// 3840 Hz, 2048 counts/g. No heap, hardware calls, or recording arrays here.
#define HAMMER_HZ 3840u
#define HAMMER_MS(ms) ((HAMMER_HZ * (ms)) / 1000u)
typedef enum {
    HAMMER_SETTLE, HAMMER_TOSS_READY, HAMMER_TOSS_FLIGHT, HAMMER_CATCH,
    HAMMER_FINAL_SETTLE, HAMMER_ARMED, HAMMER_LAUNCH, HAMMER_RECORDING
} hammer_phase_t;
typedef enum {
    HAMMER_NONE, HAMMER_RESET, HAMMER_TOSS_ACCEPTED, HAMMER_BECAME_ARMED,
    HAMMER_TRIGGERED, HAMMER_TIMED_OUT
} hammer_event_t;
typedef enum {
    HAMMER_REASON_NONE, HAMMER_NO_PUSH, HAMMER_SHORT_AIR, HAMMER_LONG_AIR,
    HAMMER_NOISY_AIR, HAMMER_NO_PAUSE, HAMMER_SEQUENCE_EXPIRED
} hammer_reason_t;
typedef struct {
    hammer_phase_t phase;
    uint32_t seq, phase_at, sequence_at, armed_at, stable, flight, gap, gaps;
    uint32_t impulse_at, launch_at, trigger_at;
    uint32_t push_samples, push_at, last_air_ms;
    int16_t reference[3];
    uint8_t tosses;
    bool reference_valid, impulse_valid, push_valid;
    hammer_reason_t reason;
} hammer_trigger_t;

static inline uint64_t hammer_mag2(int32_t x, int32_t y, int32_t z) {
    return (uint64_t)((int64_t)x*x + (int64_t)y*y + (int64_t)z*z);
}
static inline void hammer_reset(hammer_trigger_t *h) {
    *h = (hammer_trigger_t){0};
}
static inline void hammer_phase(hammer_trigger_t *h, hammer_phase_t phase) {
    h->phase = phase; h->phase_at = h->seq;
    h->stable = h->flight = h->gap = h->gaps = 0;
    h->reference_valid = false;
}
static inline bool hammer_stable(hammer_trigger_t *h, int16_t x, int16_t y, int16_t z) {
    uint64_t m = hammer_mag2(x,y,z);
    if (m < 1536ull*1536u || m > 2662ull*2662u) {
        h->reference_valid = false; h->stable = 0; return false;
    }
    if (!h->reference_valid ||
        hammer_mag2(x-h->reference[0], y-h->reference[1], z-h->reference[2]) > 410ull*410u) {
        h->reference[0]=x; h->reference[1]=y; h->reference[2]=z;
        h->reference_valid=true; h->stable=0;
    }
    ++h->stable;
    return true;
}
static inline hammer_event_t hammer_restart(hammer_trigger_t *h, hammer_event_t event) {
    uint32_t seq=h->seq, air=h->last_air_ms;
    hammer_reset(h); h->seq=seq; h->last_air_ms=air;
    return event;
}
static inline hammer_event_t hammer_reject(hammer_trigger_t *h, hammer_reason_t reason) {
    hammer_restart(h,HAMMER_RESET); h->reason=reason;
    return HAMMER_RESET;
}
static inline const char *hammer_reason_name(hammer_reason_t reason) {
    switch (reason) {
    case HAMMER_NO_PUSH: return "NO PUSH";
    case HAMMER_SHORT_AIR: return "SHORT AIR";
    case HAMMER_LONG_AIR: return "LONG AIR";
    case HAMMER_NOISY_AIR: return "AIR NOISY";
    case HAMMER_NO_PAUSE: return "NEED PAUSE";
    case HAMMER_SEQUENCE_EXPIRED: return "TRY AGAIN";
    default: return "";
    }
}

static inline hammer_event_t hammer_sample(hammer_trigger_t *h, int16_t x, int16_t y, int16_t z) {
    ++h->seq;
    uint64_t m=hammer_mag2(x,y,z);
    bool freefall=m < 922ull*922u; // 0.45 g, with bounded noise tolerance
    bool impulse=m > 3072ull*3072u; // actual launch impact remains 1.5 g
    if (impulse && h->phase != HAMMER_LAUNCH) {
        h->impulse_at=h->seq; h->impulse_valid=true;
    }
    if (h->phase == HAMMER_RECORDING) return HAMMER_NONE;
    if ((h->phase == HAMMER_ARMED || h->phase == HAMMER_LAUNCH) &&
        h->seq-h->armed_at >= HAMMER_HZ*120u)
        return hammer_restart(h,HAMMER_TIMED_OUT);
    if (h->tosses && h->phase < HAMMER_FINAL_SETTLE &&
        h->seq-h->sequence_at > HAMMER_HZ*15u)
        return hammer_reject(h,HAMMER_SEQUENCE_EXPIRED);

    switch (h->phase) {
    case HAMMER_SETTLE:
        if (hammer_stable(h,x,y,z) && h->stable >= HAMMER_MS(500)) {
            hammer_phase(h,HAMMER_TOSS_READY); h->push_valid=false; h->push_samples=0;
        }
        break;
    case HAMMER_TOSS_READY:
        // Require a modest, sustained upward push rather than a single spike.
        if (m>2458ull*2458u) {
            if (++h->push_samples>=HAMMER_MS(12)) { h->push_valid=true; h->push_at=h->seq; }
        } else h->push_samples=0;
        if (freefall) {
            if (!h->push_valid || h->seq-h->push_at > HAMMER_MS(350))
                return hammer_reject(h,HAMMER_NO_PUSH);
            hammer_phase(h,HAMMER_TOSS_FLIGHT); h->flight=1;
            h->reason=HAMMER_REASON_NONE;
        }
        break;
    case HAMMER_TOSS_FLIGHT:
        if (freefall) { ++h->flight; h->gap=0; }
        else { ++h->gap; ++h->gaps; }
        h->last_air_ms=h->flight*1000u/HAMMER_HZ;
        if (h->seq-h->phase_at>=HAMMER_MS(900)) return hammer_reject(h,HAMMER_LONG_AIR);
        // >=5 ms of support ends flight. One noisy sample cannot reject a toss.
        if (h->gap>=HAMMER_MS(5) && m>=1638ull*1638u) {
            if (h->flight<HAMMER_MS(150)) return hammer_reject(h,HAMMER_SHORT_AIR);
            hammer_phase(h,HAMMER_CATCH);
        } else if (h->gap>HAMMER_MS(10) || h->gaps>HAMMER_MS(20)) {
            return hammer_reject(h,HAMMER_NOISY_AIR);
        }
        break;
    case HAMMER_CATCH:
        if (freefall) ++h->gap; else h->gap=0;
        if (h->gap>HAMMER_MS(30) || h->seq-h->phase_at > HAMMER_MS(2500))
            return hammer_reject(h,HAMMER_NO_PAUSE);
        if (hammer_stable(h,x,y,z) && h->stable >= HAMMER_MS(500)) {
            // A soft catch need not produce an impact >1.5 g. Returning to
            // sustained support and settling is the catch confirmation.
            if (!h->tosses) h->sequence_at=h->seq;
            ++h->tosses;
            hammer_phase(h,h->tosses == 3 ? HAMMER_FINAL_SETTLE : HAMMER_TOSS_READY);
            h->push_valid=false; h->push_samples=0;
            return HAMMER_TOSS_ACCEPTED;
        }
        break;
    case HAMMER_FINAL_SETTLE:
        if (freefall || h->seq-h->phase_at > HAMMER_MS(5000))
            return hammer_reject(h,HAMMER_NO_PAUSE);
        if (hammer_stable(h,x,y,z) && h->stable >= HAMMER_MS(2000)) {
            hammer_phase(h,HAMMER_ARMED); h->armed_at=h->seq;
            h->impulse_valid=false;
            return HAMMER_BECAME_ARMED;
        }
        break;
    case HAMMER_ARMED:
        if (m < 922ull*922u && h->impulse_valid &&
            h->seq-h->impulse_at <= HAMMER_MS(150)) {
            h->trigger_at=h->impulse_at;
            h->launch_at=h->seq;
            hammer_phase(h,HAMMER_LAUNCH); h->flight=1;
        }
        break;
    case HAMMER_LAUNCH:
        if (m < 922ull*922u) { ++h->flight; h->gap=0; }
        else { ++h->gap; ++h->gaps; }
        // Allow <=5 ms interruptions and <=10 ms total, but demand 200 ms
        // of actual near-weightlessness. Isolated bumps do not lock a capture.
        if (h->gap > HAMMER_MS(5) || h->gaps > HAMMER_MS(10)) {
            hammer_phase(h,HAMMER_ARMED); h->impulse_valid=false;
        } else if (h->flight >= HAMMER_MS(200)) {
            hammer_phase(h,HAMMER_RECORDING);
            return HAMMER_TRIGGERED;
        }
        break;
    case HAMMER_RECORDING: break;
    }
    return HAMMER_NONE;
}
