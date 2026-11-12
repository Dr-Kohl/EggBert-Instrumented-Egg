#ifndef PENDULUM_DETECTOR_H
#define PENDULUM_DETECTOR_H

#include <stdbool.h>
#include <stdint.h>

#define PENDULUM_INTERVAL_COUNT 6u

typedef struct {
    bool initialized, rising, have_peak, complete, weak;
    float filtered, trough, peak, prominence, period_seconds;
    uint32_t start_ms, sample_ms, peak_ms, last_peak_ms;
    uint32_t intervals[PENDULUM_INTERVAL_COUNT];
    uint8_t interval_count;
} pendulum_detector_t;

void pendulum_detector_reset(pendulum_detector_t *detector);
void pendulum_detector_sample(pendulum_detector_t *detector, float radial_g,
                              uint32_t now_ms);

#endif
