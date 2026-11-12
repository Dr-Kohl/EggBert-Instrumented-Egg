#include "pendulum_detector.h"

#include <math.h>
#include <string.h>

#define MIN_INTERVAL_MS 300u
#define MAX_INTERVAL_MS 8000u
#define MIN_PROMINENCE_G 0.025f
#define MIN_REVERSAL_G 0.010f
#define MAX_REVERSAL_G 0.060f

void pendulum_detector_reset(pendulum_detector_t *d) {
    memset(d, 0, sizeof *d);
}

static void sorted_intervals(const pendulum_detector_t *d, uint32_t *sorted) {
    for (unsigned i = 0; i < d->interval_count; ++i) {
        unsigned j = i;
        while (j && sorted[j - 1] > d->intervals[i]) {
            sorted[j] = sorted[j - 1];
            --j;
        }
        sorted[j] = d->intervals[i];
    }
}

static uint32_t median_interval(const pendulum_detector_t *d) {
    uint32_t sorted[PENDULUM_INTERVAL_COUNT];
    sorted_intervals(d, sorted);
    unsigned n = d->interval_count;
    return n % 2 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2;
}

static bool accept_peak(pendulum_detector_t *d, uint32_t peak_ms) {
    if (d->have_peak) {
        uint32_t interval = peak_ms - d->last_peak_ms;
        // A rejected early bump must never move the accepted timing reference.
        if (interval < MIN_INTERVAL_MS) return false;
        if (d->interval_count) {
            uint32_t typical = median_interval(d);
            if ((float)interval < 0.80f * typical) return false;
            if ((float)interval > 1.20f * typical) {
                // A missed crossing or changed rhythm starts a fresh sequence;
                // never average a doubled interval into an apparent long period.
                d->interval_count = 0;
                d->last_peak_ms = peak_ms;
                return true;
            }
        }
        if (interval <= MAX_INTERVAL_MS) {
            if (d->interval_count == PENDULUM_INTERVAL_COUNT) {
                memmove(d->intervals, d->intervals + 1,
                        (PENDULUM_INTERVAL_COUNT - 1u) * sizeof d->intervals[0]);
                --d->interval_count;
            }
            d->intervals[d->interval_count++] = interval;
        } else {
            d->interval_count = 0;
        }
    }
    d->have_peak = true;
    d->last_peak_ms = peak_ms;
    if (d->interval_count == PENDULUM_INTERVAL_COUNT) {
        uint32_t sorted[PENDULUM_INTERVAL_COUNT];
        sorted_intervals(d, sorted);
        uint32_t median = (sorted[2] + sorted[3]) / 2;
        if ((float)(sorted[5] - sorted[0]) <= 0.15f * median) {
            // Six consistent half-periods; trim the shortest and longest to
            // reduce timing jitter, then convert to a complete oscillation.
            uint32_t total = sorted[1] + sorted[2] + sorted[3] + sorted[4];
            d->period_seconds = 2.0f * total / 4000.0f;
            d->complete = true;
        }
    }
    return true;
}

void pendulum_detector_sample(pendulum_detector_t *d, float radial_g,
                              uint32_t now_ms) {
    if (d->complete || d->weak) return;
    float value = fabsf(radial_g);
    if (!d->initialized) {
        d->initialized = true;
        d->filtered = d->trough = d->peak = value;
        d->start_ms = d->sample_ms = d->peak_ms = now_ms;
        return;
    }
    uint32_t elapsed = now_ms - d->sample_ms;
    d->sample_ms = now_ms;
    // Keep the filter response near 50 ms even when OLED/USB work delays a read.
    float alpha = (float)elapsed / (50.0f + elapsed);
    d->filtered += alpha * (value - d->filtered);
    value = d->filtered;
    float reversal = fminf(MAX_REVERSAL_G,
                          fmaxf(MIN_REVERSAL_G, 0.10f * d->prominence));
    if (!d->rising) {
        if (value < d->trough) d->trough = value;
        if (value - d->trough >= reversal) {
            d->rising = true;
            d->peak = value;
            d->peak_ms = now_ms;
        }
    } else if (value > d->peak) {
        d->peak = value;
        d->peak_ms = now_ms;
    } else if (d->peak - value >= reversal) {
        float prominence = d->peak - d->trough;
        d->rising = false;
        d->trough = value;
        if (prominence >= MIN_PROMINENCE_G) {
            // Track amplitude each crossing so the reversal threshold follows
            // damping, but retain a noise floor instead of chasing tiny bumps.
            if (accept_peak(d, d->peak_ms)) d->prominence = prominence;
        }
    }
    uint32_t timeout = d->interval_count ? 3u * median_interval(d) : 16000u;
    if (timeout < 3000u) timeout = 3000u;
    uint32_t reference = d->have_peak ? d->last_peak_ms : d->start_ms;
    if (!d->complete && (now_ms - reference > timeout || now_ms - d->start_ms > 60000u))
        d->weak = true;
}
