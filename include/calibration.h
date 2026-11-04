#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LIVE_ACCEL_COUNTS_PER_G 16384u
typedef struct __attribute__((packed)) {
    uint32_t magic, version, generation;
    int32_t offset_live_counts[3];
    uint32_t scale_q16[3];
    uint32_t crc32;
} calibration_record_t;

const calibration_record_t *calibration_current(void);
bool calibration_save(const calibration_record_t *candidate);
int16_t calibration_correct_axis(int16_t raw, unsigned axis, uint32_t counts_per_g);
