#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} lsm6dsv_accel_sample_t;

typedef enum {
    LSM6DSV_MODEL_UNKNOWN,
    LSM6DSV_MODEL_LSM6DSV,
    LSM6DSV_MODEL_LSM6DSV320X,
} lsm6dsv_model_t;

// One firmware supports either board. Identification remains available on failure;
// model is UNKNOWN until reset and low-g configuration have been verified.
lsm6dsv_model_t lsm6dsv_model(void);
uint8_t lsm6dsv_device_id(void);
const char *lsm6dsv_model_name(void);
// Initializes four-wire SPI, identifies 0x70/0x73, resets and verifies live mode.
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
// Drop recording: v1 stays low-g only; v2 adds +/-320 g at the same 3840 Hz.
bool lsm6dsv_drop_fifo_start(void);
bool lsm6dsv_dual_fifo_active(void);
typedef struct {
    lsm6dsv_accel_sample_t low, high;
} lsm6dsv_accel_pair_t;
typedef struct {
    uint32_t low_records, high_records, pairs, startup_discarded;
    uint32_t sequence_errors, unexpected_tags, io_errors;
    uint16_t max_level;
    bool overrun;
} lsm6dsv_dual_stats_t;
// Returns complete pairs with identical FIFO time-slot tags. A fault latches;
// callers must stop rather than silently joining samples across a data gap.
size_t lsm6dsv_dual_fifo_read(lsm6dsv_accel_pair_t *pairs, size_t capacity);
lsm6dsv_dual_stats_t lsm6dsv_dual_fifo_stats(void);
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
