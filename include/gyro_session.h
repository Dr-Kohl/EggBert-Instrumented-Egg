#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// LSM6DSV at +/-2000 deg/s: 70 millidegrees/s per raw count.
#define GYRO_MDPS_PER_COUNT 70
#define GYRO_SETTLE_MS 1000u
#define GYRO_RESET_DELAY_MS 750u
#define GYRO_ZERO_SAMPLES 120u
#define GYRO_ZERO_MAX_COUNTS 71 // approximately 5 deg/s
#define GYRO_ZERO_RANGE_COUNTS 43 // approximately 3 deg/s
#define GYRO_LIMIT_COUNTS 28500 // near the nominal +/-2000 deg/s range

typedef enum { GYRO_SETTLING, GYRO_ZEROING, GYRO_RESET_WAIT, GYRO_RUNNING } gyro_phase_t;
typedef struct {
    gyro_phase_t phase;
    uint32_t phase_started_ms;
    unsigned zero_count;
    int32_t zero_sum[3];
    int16_t zero_min[3], zero_max[3];
    int32_t bias_mdps[3], live_mdps[3], peak_mdps[3];
    bool live_limit, peak_limit;
} gyro_session_t;

static inline int32_t gyro_abs(int32_t value) { return value < 0 ? -value : value; }

static inline void gyro_session_begin(gyro_session_t *s, uint32_t now) {
    memset(s, 0, sizeof(*s));
    s->phase = GYRO_SETTLING;
    s->phase_started_ms = now;
}

// Reset the trial, not the stationary zero. Ignore button handling for 750 ms.
static inline void gyro_session_reset(gyro_session_t *s, uint32_t now) {
    memset(s->peak_mdps, 0, sizeof(s->peak_mdps));
    s->peak_limit = false;
    if (s->phase == GYRO_RUNNING || s->phase == GYRO_RESET_WAIT) {
        s->phase = GYRO_RESET_WAIT;
        s->phase_started_ms = now;
    }
}

// Feed only fresh samples. Unsigned elapsed time also handles clock wraparound.
static inline void gyro_session_sample(gyro_session_t *s, const int16_t raw[3], uint32_t now) {
    if (s->phase == GYRO_SETTLING) {
        if ((uint32_t)(now - s->phase_started_ms) < GYRO_SETTLE_MS) return;
        s->phase = GYRO_ZEROING;
    }
    if (s->phase == GYRO_ZEROING) {
        bool moving = false;
        for (unsigned i = 0; i < 3; ++i) {
            if (s->zero_count == 0) s->zero_min[i] = s->zero_max[i] = raw[i];
            if (raw[i] < s->zero_min[i]) s->zero_min[i] = raw[i];
            if (raw[i] > s->zero_max[i]) s->zero_max[i] = raw[i];
            moving |= gyro_abs(raw[i]) > GYRO_ZERO_MAX_COUNTS ||
                      s->zero_max[i] - s->zero_min[i] > GYRO_ZERO_RANGE_COUNTS;
        }
        if (moving) {
            s->zero_count = 0;
            memset(s->zero_sum, 0, sizeof(s->zero_sum));
            return;
        }
        for (unsigned i = 0; i < 3; ++i) s->zero_sum[i] += raw[i];
        if (++s->zero_count == GYRO_ZERO_SAMPLES) {
            for (unsigned i = 0; i < 3; ++i)
                s->bias_mdps[i] = s->zero_sum[i] * GYRO_MDPS_PER_COUNT / (int32_t)GYRO_ZERO_SAMPLES;
            s->phase = GYRO_RUNNING;
        }
        return;
    }
    for (unsigned i = 0; i < 3; ++i)
        s->live_mdps[i] = (int32_t)raw[i] * GYRO_MDPS_PER_COUNT - s->bias_mdps[i];
    s->live_limit = false;
    for (unsigned i = 0; i < 3; ++i) s->live_limit |= gyro_abs(raw[i]) >= GYRO_LIMIT_COUNTS;
    if (s->phase == GYRO_RESET_WAIT) {
        if ((uint32_t)(now - s->phase_started_ms) < GYRO_RESET_DELAY_MS) return;
        s->phase = GYRO_RUNNING;
    }
    for (unsigned i = 0; i < 3; ++i)
        if (gyro_abs(s->live_mdps[i]) > gyro_abs(s->peak_mdps[i]))
            s->peak_mdps[i] = s->live_mdps[i];
    s->peak_limit |= s->live_limit;
}
