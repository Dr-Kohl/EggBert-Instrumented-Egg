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

// On-demand gyro: 240 Hz, +/-2000 deg/s, 70 mdps/count. Never batched into
// the accelerometer capture FIFO. read_gyro returns false without fresh data.
bool lsm6dsv_gyro_start(void);
void lsm6dsv_gyro_stop(void);
bool lsm6dsv_read_gyro(int16_t *x, int16_t *y, int16_t *z);

typedef struct { float w, x, y, z; } lsm6dsv_quaternion_t;
// Exclusive FIFO mode: 60 Hz on-chip game rotation, +/-4 g and +/-2000 dps.
// Stop restores the preceding live sensor configuration. Never use during capture.
bool lsm6dsv_orientation_start(void);
void lsm6dsv_orientation_stop(void);
bool lsm6dsv_orientation_read(lsm6dsv_quaternion_t *quaternion, bool *overrun);

// Starts FIFO acquisition of accelerometer-only samples at 3.84 kHz and +/-16 g.
// FIFO watermark and overrun are routed to IMU_INT1_PIN.
bool lsm6dsv_fifo_start(void);
// Beam acquisition: 480 Hz, +/-4 g, same accelerometer-only FIFO.
bool lsm6dsv_beam_fifo_start(void);

// Reads CTRL8, which contains the current accelerometer full-scale selection.
uint8_t lsm6dsv_read_ctrl8(void);

// Stops FIFO acquisition and returns the sensor to the 120 Hz live-read setup.
void lsm6dsv_fifo_stop(void);

// Reads up to max_samples queued FIFO records.  Each record is raw signed X/Y/Z
// acceleration counts.  *overrun is set if the hardware FIFO has overflowed.
size_t lsm6dsv_fifo_read(lsm6dsv_accel_sample_t *samples, size_t max_samples,
                         bool *overrun);

// True if the FIFO watermark/overrun interrupt has occurred since the last read.
bool lsm6dsv_fifo_irq_pending(void);
