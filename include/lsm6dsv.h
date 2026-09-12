#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} lsm6dsv_accel_sample_t;

// Initializes the four-wire SPI interface and confirms the fixed 0x70 ID.
bool lsm6dsv_init(void);

// Reads raw signed acceleration counts. At the configured +/-2 g scale: 0.061 mg/LSB.
bool lsm6dsv_read_accel(int16_t *x, int16_t *y, int16_t *z);

// Starts FIFO acquisition of accelerometer-only samples at 3.84 kHz and +/-16 g.
// FIFO watermark and overrun are routed to IMU_INT1_PIN.
bool lsm6dsv_fifo_start(void);

// Stops FIFO acquisition and returns the sensor to the 120 Hz live-read setup.
void lsm6dsv_fifo_stop(void);

// Reads up to max_samples queued FIFO records.  Each record is raw signed X/Y/Z
// acceleration counts.  *overrun is set if the hardware FIFO has overflowed.
size_t lsm6dsv_fifo_read(lsm6dsv_accel_sample_t *samples, size_t max_samples,
                         bool *overrun);

// True if the FIFO watermark/overrun interrupt has occurred since the last read.
bool lsm6dsv_fifo_irq_pending(void);
