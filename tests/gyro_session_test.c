#include <assert.h>
#include <stdio.h>
#include "gyro_session.h"

static void zero_session(gyro_session_t *s, const int16_t raw[3], uint32_t start) {
    gyro_session_begin(s, start);
    for (unsigned n = 0; n < GYRO_ZERO_SAMPLES; ++n)
        gyro_session_sample(s, raw, start + GYRO_SETTLE_MS + n * 5u);
    assert(s->phase == GYRO_RUNNING);
}

int main(void) {
    gyro_session_t s;
    const int16_t bias[3] = {10, -12, 3};
    const int16_t spin[3] = {3010, -12, 3}; // +210 deg/s after zeroing
    zero_session(&s, bias, 0);
    assert(s.bias_mdps[0] == 700 && s.bias_mdps[1] == -840);
    gyro_session_sample(&s, bias, 1700);
    assert(s.live_mdps[0] == 0 && s.live_mdps[1] == 0 && s.live_mdps[2] == 0);
    gyro_session_sample(&s, spin, 1800);
    assert(s.peak_mdps[0] == 210000);
    const int16_t reverse[3] = {-6990, 1012, -1997};
    gyro_session_sample(&s, reverse, 1900);
    assert(s.peak_mdps[0] == -490000); // larger negative wins
    assert(s.peak_mdps[1] == 71680 && s.peak_mdps[2] == -140000);
    gyro_session_sample(&s, bias, 2000);
    assert(s.peak_mdps[0] == -490000 && s.live_mdps[0] == 0); // retained at rest

    gyro_session_reset(&s, 2100);
    assert(s.peak_mdps[0] == 0 && s.bias_mdps[0] == 700);
    gyro_session_sample(&s, reverse, 2849);
    assert(s.peak_mdps[0] == 0 && s.phase == GYRO_RESET_WAIT);
    gyro_session_sample(&s, spin, 2850);
    assert(s.peak_mdps[0] == 210000 && s.phase == GYRO_RUNNING);
    const int16_t limit[3] = {-32768, 0, 32767};
    gyro_session_sample(&s, limit, 2900);
    assert(s.peak_limit && s.live_limit && s.peak_mdps[0] < 0);
    gyro_session_sample(&s, bias, 3000);
    assert(!s.live_limit && s.peak_limit);
    gyro_session_reset(&s, 3100);
    assert(!s.peak_limit);

    // Steady rotation must not be accepted as the stationary zero.
    gyro_session_begin(&s, 0);
    for (unsigned n = 0; n < 200; ++n) gyro_session_sample(&s, spin, 1000 + n * 5);
    assert(s.phase == GYRO_ZEROING && s.zero_count == 0);
    for (unsigned n = 0; n < 60; ++n) gyro_session_sample(&s, bias, 2100 + n * 5);
    const int16_t movement[3] = {60, -12, 3}; // under rate limit, over range limit
    gyro_session_sample(&s, movement, 2400);
    assert(s.zero_count == 0);
    for (unsigned n = 0; n < GYRO_ZERO_SAMPLES; ++n) gyro_session_sample(&s, bias, 2500 + n * 5);
    assert(s.phase == GYRO_RUNNING);

    // Deadlines continue working when the millisecond clock wraps.
    uint32_t near_wrap = UINT32_MAX - 500u;
    zero_session(&s, bias, near_wrap);
    gyro_session_reset(&s, near_wrap);
    gyro_session_sample(&s, spin, near_wrap + 749u);
    assert(s.peak_mdps[0] == 0);
    gyro_session_sample(&s, spin, near_wrap + 750u);
    assert(s.peak_mdps[0] == 210000);
    puts("Gyro session tests passed");
}
