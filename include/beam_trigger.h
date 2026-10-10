#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define BEAM_RATE_HZ 480u
#define BEAM_SECONDS 15u
#define BEAM_COUNTS_PER_G 16384u
#define BEAM_PRE_SAMPLES (BEAM_RATE_HZ / 2u)
#define BEAM_TOTAL_SAMPLES (BEAM_RATE_HZ * BEAM_SECONDS)
#define BEAM_TRIGGER_COUNTS 3277u // 0.2 g, rounded upward
#define BEAM_TRIGGER_SAMPLES 5u   // 10.4 ms at 480 Hz

typedef struct {
    uint32_t count, above;
    int32_t minimum[3], maximum[3], baseline[3];
    int64_t sum[3];
    bool ready;
} beam_trigger_t;

static inline void beam_trigger_reset(beam_trigger_t *state) {
    memset(state, 0, sizeof *state);
}

// Baseline is fixed after one second within a 0.04 g vector range.
// A movement restarts settling. Five consecutive vector changes >=0.2 g
// trigger; a single spike or a short disturbance does not.
static inline bool beam_trigger_sample(beam_trigger_t *state, int16_t x, int16_t y, int16_t z) {
    int32_t v[3] = {x, y, z};
    if (!state->ready) {
        for (unsigned a = 0; a < 3; ++a) {
            if (!state->count) state->minimum[a] = state->maximum[a] = v[a];
            if (v[a] < state->minimum[a]) state->minimum[a] = v[a];
            if (v[a] > state->maximum[a]) state->maximum[a] = v[a];
        }
        int64_t range_squared = 0;
        for (unsigned a = 0; a < 3; ++a) {
            int64_t d = state->maximum[a] - state->minimum[a];
            range_squared += d*d;
        }
        if (range_squared > 655LL*655LL) {
            beam_trigger_reset(state);
            return false;
        }
        for (unsigned a = 0; a < 3; ++a) state->sum[a] += v[a];
        if (++state->count == BEAM_RATE_HZ) {
            for (unsigned a = 0; a < 3; ++a) state->baseline[a] = (int32_t)(state->sum[a] / BEAM_RATE_HZ);
            state->ready = true;
        }
        return false;
    }
    int64_t change_squared = 0;
    for (unsigned a = 0; a < 3; ++a) {
        int64_t d = v[a] - state->baseline[a];
        change_squared += d*d;
    }
    if (change_squared >= (int64_t)BEAM_TRIGGER_COUNTS*BEAM_TRIGGER_COUNTS)
        return ++state->above >= BEAM_TRIGGER_SAMPLES;
    state->above = 0;
    return false;
}
