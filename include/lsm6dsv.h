#pragma once

#include <stdbool.h>
#include <stdint.h>

// Initializes the four-wire SPI interface and confirms the fixed 0x70 ID.
bool lsm6dsv_init(void);

// Reads raw signed acceleration counts. At the configured +/-2 g scale: 0.061 mg/LSB.
bool lsm6dsv_read_accel(int16_t *x, int16_t *y, int16_t *z);
