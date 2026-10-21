#pragma once
#include <stdbool.h>
#include <stdint.h>

// Switch the entire XYZ vector together. Raw low-g also guards against a
// calibration correction masking the approach to the hardware range limit.
static inline bool accel_select_high(bool previous, uint32_t low_peak_counts,
                                     uint32_t raw_low_peak_counts, uint32_t high_peak_counts) {
    if (!previous) return low_peak_counts >= 12u*2048u || raw_low_peak_counts >= 12u*2048u;
    return !(high_peak_counts*10417u < 8000000u &&
             low_peak_counts < 8u*2048u && raw_low_peak_counts < 8u*2048u);
}

// Preserve the existing Gentle Catch units, with a wider integer for >16 g.
static inline uint32_t accel_high_peak_low_counts(uint32_t peak) {
    return (uint32_t)(((uint64_t)peak*10417u*2048u+500000u)/1000000u);
}
