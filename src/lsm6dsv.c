#include "lsm6dsv.h"

#include "board.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include <math.h>
#include "pico/stdlib.h"

#define LSM6DSV_WHO_AM_I       0x0Fu
#define LSM6DSV320X_CTRL1_XL_HG 0x4Eu
#define LSM6DSV320X_COUNTER_BDR_REG1 0x0Bu
#define LSM6DSV_CTRL1          0x10u
#define LSM6DSV_CTRL2          0x11u
#define LSM6DSV_CTRL3          0x12u
#define LSM6DSV_CTRL6          0x15u
#define LSM6DSV_STATUS_REG     0x1Eu
#define LSM6DSV_OUTX_L_G       0x22u
#define LSM6DSV_CTRL8          0x17u
#define LSM6DSV_OUTX_L_A       0x28u
#define LSM6DSV_FIFO_CTRL1     0x07u
#define LSM6DSV_FIFO_CTRL2     0x08u
#define LSM6DSV_FIFO_CTRL3     0x09u
#define LSM6DSV_FIFO_CTRL4     0x0Au
#define LSM6DSV_INT1_CTRL      0x0Du
#define LSM6DSV_FIFO_STATUS1   0x1Bu
#define LSM6DSV_FIFO_STATUS2   0x1Cu
#define LSM6DSV_FIFO_DATA_TAG  0x78u

#define LSM6DSV_FIFO_WATERMARK_SAMPLES 32u
#define FIFO_STATUS_OVERRUN             0x40u
#define FIFO_STATUS_OVERRUN_LATCHED     0x08u

static volatile bool fifo_irq_pending;
static lsm6dsv_model_t detected_model;
static uint8_t detected_id;
static bool dual_running;
static lsm6dsv_dual_stats_t dual_stats;
static lsm6dsv_accel_pair_t pending_pair;
static uint8_t pending_mask, pending_slot, previous_slot;
static bool have_pair;
static bool orientation_running;
static const uint8_t orientation_regs[] = {0x07, 0x08, 0x09, 0x0A, 0x0D, 0x10, 0x11, 0x12, 0x15, 0x17};
static uint8_t orientation_saved[sizeof orientation_regs];
static uint8_t saved_emb_enable, saved_emb_fifo, saved_sflp_odr;

static void select_imu(bool selected) { gpio_put(IMU_CS_PIN, !selected); }

static uint8_t read_reg(uint8_t reg) {
    uint8_t tx[] = { (uint8_t)(reg | 0x80u), 0u };
    uint8_t rx[sizeof tx] = {0};
    select_imu(true);
    spi_write_read_blocking(IMU_SPI, tx, rx, sizeof tx);
    select_imu(false);
    return rx[1];
}

static void write_reg(uint8_t reg, uint8_t value) {
    uint8_t tx[] = { (uint8_t)(reg & 0x7fu), value };
    select_imu(true);
    spi_write_blocking(IMU_SPI, tx, sizeof tx);
    select_imu(false);
}

lsm6dsv_model_t lsm6dsv_model(void) { return detected_model; }
uint8_t lsm6dsv_device_id(void) { return detected_id; }
const char *lsm6dsv_model_name(void) {
    switch (detected_model) {
    case LSM6DSV_MODEL_LSM6DSV: return "LSM6DSV";
    case LSM6DSV_MODEL_LSM6DSV320X: return "LSM6DSV320X";
    default: return "UNKNOWN";
    }
}

static void imu_irq_callback(uint gpio, uint32_t events) {
    if (gpio == IMU_INT1_PIN && (events & GPIO_IRQ_EDGE_RISE)) fifo_irq_pending = true;
}

static uint16_t fifo_level(bool *overrun) {
    uint8_t low = read_reg(LSM6DSV_FIFO_STATUS1);
    uint8_t high_and_flags = read_reg(LSM6DSV_FIFO_STATUS2);
    if (overrun && (high_and_flags & (FIFO_STATUS_OVERRUN | FIFO_STATUS_OVERRUN_LATCHED)))
        *overrun = true;
    return (uint16_t)low | ((uint16_t)(high_and_flags & 0x01u) << 8);
}

static bool read_fifo_sample(lsm6dsv_accel_sample_t *sample) {
    // A FIFO record is a tag plus six data bytes.  Only accelerometer data is
    // batched in this first bring-up, so every FIFO record is an X/Y/Z sample.
    uint8_t tx[8] = { (uint8_t)(LSM6DSV_FIFO_DATA_TAG | 0x80u), 0, 0, 0, 0, 0, 0, 0 };
    uint8_t rx[sizeof tx] = {0};
    select_imu(true);
    int transferred = spi_write_read_blocking(IMU_SPI, tx, rx, sizeof tx);
    select_imu(false);
    if (transferred != sizeof tx) return false;

    // Capture must never interpret a fusion or gyro record as acceleration.
    if ((rx[1] >> 3) != 0x02u) return false;

    sample->x = (int16_t)((uint16_t)rx[2] | ((uint16_t)rx[3] << 8));
    sample->y = (int16_t)((uint16_t)rx[4] | ((uint16_t)rx[5] << 8));
    sample->z = (int16_t)((uint16_t)rx[6] | ((uint16_t)rx[7] << 8));
    return true;
}

bool lsm6dsv_init(void) {
    dual_running = false;
    detected_model = LSM6DSV_MODEL_UNKNOWN;
    detected_id = 0;
    orientation_running = false;
    fifo_irq_pending = false;
    spi_init(IMU_SPI, IMU_SPI_BAUD);
    gpio_set_function(IMU_MISO_PIN, GPIO_FUNC_SPI);
    gpio_set_function(IMU_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(IMU_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_init(IMU_CS_PIN);
    gpio_set_dir(IMU_CS_PIN, GPIO_OUT);
    select_imu(false);
    gpio_init(IMU_INT1_PIN); gpio_set_dir(IMU_INT1_PIN, GPIO_IN);
    gpio_init(IMU_INT2_PIN); gpio_set_dir(IMU_INT2_PIN, GPIO_IN);
    spi_set_format(IMU_SPI, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);

    gpio_set_irq_enabled_with_callback(IMU_INT1_PIN, GPIO_IRQ_EDGE_RISE, false,
                                       &imu_irq_callback);
    // Allow power-up settling; never configure an unknown device.
    sleep_ms(20);
    for (unsigned attempt = 0; attempt < 5; ++attempt) {
        detected_id = read_reg(LSM6DSV_WHO_AM_I);
        if (detected_id == 0x70u || detected_id == 0x73u) break;
        sleep_ms(10);
    }
    if (detected_id != 0x70u && detected_id != 0x73u) return false;

    // Both devices share SW_RESET, BDU, IF_INC and the low-g/gyro register map.
    // Reset prevents a preceding firmware session from leaving high-g or fusion on.
    write_reg(LSM6DSV_CTRL3, 0x01u);
    unsigned reset_wait;
    for (reset_wait = 0; reset_wait < 100; ++reset_wait) {
        sleep_ms(1);
        if (!(read_reg(LSM6DSV_CTRL3) & 0x01u)) break;
    }
    if (reset_wait == 100 || read_reg(LSM6DSV_WHO_AM_I) != detected_id) return false;
    write_reg(LSM6DSV_CTRL3, 0x44u); // coherent XYZ burst reads
    write_reg(LSM6DSV_CTRL2, 0x00u); // gyro remains on-demand
    write_reg(LSM6DSV_CTRL8, 0x00u); // +/-2 g live mode on both boards
    // Advanced capture is a later milestone: explicitly leave high-g powered down.
    if (detected_id == 0x73u) write_reg(LSM6DSV320X_CTRL1_XL_HG, 0x00u);
    write_reg(LSM6DSV_CTRL1, 0x06u);
    if (read_reg(LSM6DSV_CTRL3) != 0x44u || read_reg(LSM6DSV_CTRL2) != 0 ||
        read_reg(LSM6DSV_CTRL8) != 0 || read_reg(LSM6DSV_CTRL1) != 0x06u ||
        (detected_id == 0x73u && read_reg(LSM6DSV320X_CTRL1_XL_HG) != 0)) return false;
    detected_model = detected_id == 0x70u ? LSM6DSV_MODEL_LSM6DSV : LSM6DSV_MODEL_LSM6DSV320X;
    sleep_ms(20); // allow fresh live samples before calibration/lab consumers
    return true;
}

bool lsm6dsv_read_accel(int16_t *x, int16_t *y, int16_t *z) {
    if (detected_model == LSM6DSV_MODEL_UNKNOWN) return false;
    uint8_t tx[7] = { (uint8_t)(LSM6DSV_OUTX_L_A | 0x80u), 0, 0, 0, 0, 0, 0 };
    uint8_t rx[sizeof tx] = {0};
    select_imu(true);
    int transferred = spi_write_read_blocking(IMU_SPI, tx, rx, sizeof tx);
    select_imu(false);
    if (transferred != sizeof tx) return false;
    *x = (int16_t)((uint16_t)rx[1] | ((uint16_t)rx[2] << 8));
    *y = (int16_t)((uint16_t)rx[3] | ((uint16_t)rx[4] << 8));
    *z = (int16_t)((uint16_t)rx[5] | ((uint16_t)rx[6] << 8));
    return true;
}

bool lsm6dsv_gyro_start(void) {
    if (detected_model == LSM6DSV_MODEL_UNKNOWN) return false;
    // BDU prevents mixed low/high bytes; IF_INC supports XYZ burst reads.
    write_reg(LSM6DSV_CTRL3, read_reg(LSM6DSV_CTRL3) | 0x44u);
    write_reg(LSM6DSV_CTRL6, 0x04u); // +/-2000 deg/s (70 mdps/count)
    write_reg(LSM6DSV_CTRL2, 0x07u); // 240 Hz, high-performance mode
    return read_reg(LSM6DSV_CTRL2) == 0x07u && read_reg(LSM6DSV_CTRL6) == 0x04u;
}

void lsm6dsv_gyro_stop(void) {
    if (detected_model != LSM6DSV_MODEL_UNKNOWN) write_reg(LSM6DSV_CTRL2, 0x00u);
}

bool lsm6dsv_read_gyro(int16_t *x, int16_t *y, int16_t *z) {
    if (detected_model == LSM6DSV_MODEL_UNKNOWN) return false;
    if (!(read_reg(LSM6DSV_STATUS_REG) & 0x02u)) return false;
    uint8_t tx[7] = { (uint8_t)(LSM6DSV_OUTX_L_G | 0x80u), 0, 0, 0, 0, 0, 0 };
    uint8_t rx[sizeof tx] = {0};
    select_imu(true);
    int transferred = spi_write_read_blocking(IMU_SPI, tx, rx, sizeof tx);
    select_imu(false);
    if (transferred != sizeof tx) return false;
    *x = (int16_t)((uint16_t)rx[1] | ((uint16_t)rx[2] << 8));
    *y = (int16_t)((uint16_t)rx[3] | ((uint16_t)rx[4] << 8));
    *z = (int16_t)((uint16_t)rx[5] | ((uint16_t)rx[6] << 8));
    return true;
}

static bool fifo_start_config(uint8_t rate, uint8_t range) {
    if (detected_model == LSM6DSV_MODEL_UNKNOWN) return false;
    lsm6dsv_orientation_stop();
    dual_running = false;
    if (detected_model == LSM6DSV_MODEL_LSM6DSV320X) {
        write_reg(LSM6DSV320X_CTRL1_XL_HG, 0);
        write_reg(LSM6DSV320X_COUNTER_BDR_REG1,
                  read_reg(LSM6DSV320X_COUNTER_BDR_REG1) & ~0x08u);
    }
    // Reset the FIFO, then configure it before enabling the high-rate sensor.
    write_reg(LSM6DSV_FIFO_CTRL4, 0x00u); // bypass mode
    write_reg(LSM6DSV_INT1_CTRL, 0x00u);
    write_reg(LSM6DSV_FIFO_CTRL1, LSM6DSV_FIFO_WATERMARK_SAMPLES);
    write_reg(LSM6DSV_FIFO_CTRL2, 0x00u);
    write_reg(LSM6DSV_FIFO_CTRL3, rate);
    write_reg(LSM6DSV_CTRL8, range);
    write_reg(LSM6DSV_CTRL1, rate);
    if (read_reg(LSM6DSV_CTRL1) != rate || read_reg(LSM6DSV_CTRL8) != range ||
        read_reg(LSM6DSV_FIFO_CTRL3) != rate) {
        lsm6dsv_fifo_stop();
        return false;
    }
    write_reg(LSM6DSV_INT1_CTRL, 0x18u);  // FIFO watermark and FIFO overrun
    fifo_irq_pending = false;
    gpio_set_irq_enabled(IMU_INT1_PIN, GPIO_IRQ_EDGE_RISE, true);
    write_reg(LSM6DSV_FIFO_CTRL4, 0x06u); // continuous FIFO mode
    return true;
}

uint8_t lsm6dsv_read_ctrl8(void) { return read_reg(LSM6DSV_CTRL8); }

void lsm6dsv_fifo_stop(void) {
    if (detected_model == LSM6DSV_MODEL_UNKNOWN) return;
    gpio_set_irq_enabled(IMU_INT1_PIN, GPIO_IRQ_EDGE_RISE, false);
    write_reg(LSM6DSV_INT1_CTRL, 0x00u);
    write_reg(LSM6DSV_FIFO_CTRL4, 0x00u); // bypass / discard unread FIFO data
    if (detected_model == LSM6DSV_MODEL_LSM6DSV320X) {
        write_reg(LSM6DSV320X_CTRL1_XL_HG, 0);
        write_reg(LSM6DSV320X_COUNTER_BDR_REG1,
                  read_reg(LSM6DSV320X_COUNTER_BDR_REG1) & ~0x08u);
    }
    dual_running = false;
    write_reg(LSM6DSV_CTRL8, 0x00u);      // restore +/-2 g
    write_reg(LSM6DSV_CTRL1, 0x06u);      // restore 120 Hz live readout
    fifo_irq_pending = false;
    sleep_ms(20); // do not expose a stale +/-16g sample as a fresh +/-2g reading
}

size_t lsm6dsv_fifo_read(lsm6dsv_accel_sample_t *samples, size_t max_samples,
                         bool *overrun) {
    if (detected_model == LSM6DSV_MODEL_UNKNOWN || !samples || max_samples == 0) return 0;
    fifo_irq_pending = false;
    uint16_t available = fifo_level(overrun);
    if (available > max_samples) available = (uint16_t)max_samples;

    size_t read = 0;
    while (read < available && read_fifo_sample(&samples[read])) ++read;
    return read;
}

bool lsm6dsv_fifo_irq_pending(void) {
    return fifo_irq_pending;
}

// Shared SFLP addresses/bits checked against ST's lsm6dsv-pid and
// lsm6dsv320x-pid drivers; high-g is not involved in orientation.
// Embedded function registers require FUNC_CFG_ACCESS bit 7; always return
// to the main bank before servicing any other IMU operation.
bool lsm6dsv_orientation_start(void) {
    if (detected_model == LSM6DSV_MODEL_UNKNOWN) return false;
    if (orientation_running) return true;
    for (size_t i = 0; i < sizeof orientation_regs; ++i)
        orientation_saved[i] = read_reg(orientation_regs[i]);
    orientation_running = true;
    write_reg(LSM6DSV_FIFO_CTRL4, 0);
    write_reg(LSM6DSV_INT1_CTRL, 0);
    write_reg(LSM6DSV_FIFO_CTRL1, 1);
    write_reg(LSM6DSV_FIFO_CTRL2, 0);
    write_reg(LSM6DSV_FIFO_CTRL3, 0); // no raw acceleration/gyro batching
    write_reg(LSM6DSV_CTRL3, read_reg(LSM6DSV_CTRL3) | 0x44u);
    write_reg(LSM6DSV_CTRL8, 0x01u); // +/-4 g
    write_reg(LSM6DSV_CTRL6, 0x04u); // +/-2000 dps
    write_reg(LSM6DSV_CTRL1, 0x05u); // 60 Hz
    write_reg(LSM6DSV_CTRL2, 0x05u);
    write_reg(0x01u, 0x80u);
    saved_emb_enable = read_reg(0x04u);
    saved_emb_fifo = read_reg(0x44u);
    saved_sflp_odr = read_reg(0x5Eu);
    write_reg(0x5Eu, (saved_sflp_odr & ~0x38u) | 0x10u); // SFLP 60 Hz
    write_reg(0x44u, (saved_emb_fifo & ~0x32u) | 0x02u); // quaternion only
    write_reg(0x04u, saved_emb_enable | 0x02u);
    write_reg(0x66u, 0x02u); // initialize game rotation for this session
    bool configured = (read_reg(0x04u) & 0x02u) &&
        (read_reg(0x44u) & 0x32u) == 0x02u && (read_reg(0x5Eu) & 0x38u) == 0x10u;
    write_reg(0x01u, 0);
    configured = configured && read_reg(LSM6DSV_CTRL1) == 5 && read_reg(LSM6DSV_CTRL2) == 5;
    if (!configured) { lsm6dsv_orientation_stop(); return false; }
    write_reg(LSM6DSV_FIFO_CTRL4, 0x06u);
    return true;
}

bool lsm6dsv_fifo_start(void) { return fifo_start_config(0x0Bu, 0x03u); }
bool lsm6dsv_beam_fifo_start(void) { return fifo_start_config(0x08u, 0x01u); }

bool lsm6dsv_dual_fifo_active(void) { return dual_running; }
lsm6dsv_dual_stats_t lsm6dsv_dual_fifo_stats(void) { return dual_stats; }

bool lsm6dsv_drop_fifo_start(void) {
    if (detected_model != LSM6DSV_MODEL_LSM6DSV320X) return lsm6dsv_fifo_start();
    if (!lsm6dsv_fifo_start()) return false;
    // Settle both sensors while FIFO is bypassed so startup samples cannot fill it.
    write_reg(LSM6DSV_FIFO_CTRL4, 0);
    write_reg(LSM6DSV320X_CTRL1_XL_HG, 0x34u); // ODR=6 (3840 Hz), FS=4 (+/-320g)
    write_reg(LSM6DSV320X_COUNTER_BDR_REG1,
              read_reg(LSM6DSV320X_COUNTER_BDR_REG1) | 0x08u);
    if (read_reg(LSM6DSV320X_CTRL1_XL_HG) != 0x34u ||
        !(read_reg(LSM6DSV320X_COUNTER_BDR_REG1) & 0x08u)) {
        lsm6dsv_fifo_stop(); return false;
    }
    sleep_ms(100);
    dual_stats = (lsm6dsv_dual_stats_t){0};
    pending_mask = 0;
    have_pair = false;
    fifo_irq_pending = false;
    dual_running = true;
    write_reg(LSM6DSV_FIFO_CTRL4, 0x06u);
    return true;
}

size_t lsm6dsv_dual_fifo_read(lsm6dsv_accel_pair_t *pairs, size_t capacity) {
    if (!dual_running || !pairs || !capacity) return 0;
    fifo_irq_pending = false;
    uint16_t available = fifo_level(&dual_stats.overrun);
    if (available > dual_stats.max_level) dual_stats.max_level = available;
    if (dual_stats.overrun || dual_stats.sequence_errors || dual_stats.unexpected_tags ||
        dual_stats.io_errors) return 0;
    size_t count = 0;
    for (uint16_t i = 0; i < available && count < capacity; ++i) {
        uint8_t tx[8] = {LSM6DSV_FIFO_DATA_TAG | 0x80u}, rx[8] = {0};
        select_imu(true);
        int transferred = spi_write_read_blocking(IMU_SPI, tx, rx, sizeof tx);
        select_imu(false);
        if (transferred != sizeof tx) { ++dual_stats.io_errors; break; }
        uint8_t tag = rx[1] >> 3, slot = (rx[1] >> 1) & 3u;
        // AN6119 section 9.4: TAG_CNT identifies slots at the fastest batch rate.
        // Configuration-change records at FIFO startup carry no acceleration.
        if (tag == 0x05u && !have_pair && !pending_mask) continue;
        if (tag != 0x02u && tag != 0x1Du) { ++dual_stats.unexpected_tags; break; }
        if (tag == 0x02u) ++dual_stats.low_records; else ++dual_stats.high_records;
        if (pending_mask && slot != pending_slot) {
            if (have_pair) { ++dual_stats.sequence_errors; break; }
            ++dual_stats.startup_discarded;
            pending_mask = 0; // first slot may begin before both sources batch
        }
        if (!pending_mask) {
            if (have_pair && slot != ((previous_slot + 1u) & 3u)) {
                ++dual_stats.sequence_errors; break;
            }
            pending_slot = slot;
        }
        uint8_t mask = tag == 0x02u ? 1u : 2u;
        if (pending_mask & mask) { ++dual_stats.sequence_errors; break; }
        lsm6dsv_accel_sample_t *s = tag == 0x02u ? &pending_pair.low : &pending_pair.high;
        s->x = (int16_t)((uint16_t)rx[2] | (uint16_t)rx[3] << 8);
        s->y = (int16_t)((uint16_t)rx[4] | (uint16_t)rx[5] << 8);
        s->z = (int16_t)((uint16_t)rx[6] | (uint16_t)rx[7] << 8);
        pending_mask |= mask;
        if (pending_mask == 3u) {
            pairs[count++] = pending_pair;
            pending_mask = 0;
            previous_slot = slot;
            have_pair = true;
            ++dual_stats.pairs;
        }
    }
    return count;
}

void lsm6dsv_orientation_stop(void) {
    if (!orientation_running) return;
    write_reg(LSM6DSV_FIFO_CTRL4, 0);
    write_reg(0x01u, 0x80u);
    write_reg(0x04u, saved_emb_enable);
    write_reg(0x44u, saved_emb_fifo);
    write_reg(0x5Eu, saved_sflp_odr);
    write_reg(0x01u, 0);
    for (size_t i = 0; i < sizeof orientation_regs; ++i)
        write_reg(orientation_regs[i], orientation_saved[i]);
    orientation_running = false;
    // Allow a fresh +/-2 g sample before live lab/calibration consumers resume.
    sleep_ms(20);
}

static float fifo_half(uint8_t low, uint8_t high) {
    uint16_t bits = (uint16_t)low | ((uint16_t)high << 8);
    unsigned exponent = (bits >> 10) & 31u, fraction = bits & 1023u;
    float value = exponent == 31u ? NAN : exponent == 0u ?
        ldexpf((float)fraction, -24) : ldexpf((float)(1024u + fraction), (int)exponent - 25);
    return bits & 0x8000u ? -value : value;
}

bool lsm6dsv_orientation_read(lsm6dsv_quaternion_t *q, bool *overrun) {
    if (!orientation_running || !q) return false;
    uint16_t available = fifo_level(overrun);
    bool found = false;
    // Drain a bounded snapshot and retain the newest rotation, avoiding backlog.
    for (uint16_t i = 0; i < available; ++i) {
        uint8_t tx[8] = {LSM6DSV_FIFO_DATA_TAG | 0x80u}, rx[8] = {0};
        select_imu(true);
        int transferred = spi_write_read_blocking(IMU_SPI, tx, rx, sizeof tx);
        select_imu(false);
        if (transferred != sizeof tx) break;
        if ((rx[1] >> 3) != 0x13u) continue;
        float x = fifo_half(rx[2], rx[3]), y = fifo_half(rx[4], rx[5]), z = fifo_half(rx[6], rx[7]);
        float norm = x*x + y*y + z*z;
        if (!isfinite(norm) || norm > 1.01f) continue;
        // Half precision can round a unit vector slightly above one (ST example).
        if (norm > 1.0f) { float scale = 1.0f / sqrtf(norm); x *= scale; y *= scale; z *= scale; norm = 1.0f; }
        *q = (lsm6dsv_quaternion_t){sqrtf(1.0f - norm), x, y, z};
        found = true;
    }
    return found;
}
