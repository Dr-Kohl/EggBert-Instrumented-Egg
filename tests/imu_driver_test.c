// Exercise the real driver through a simulated SPI device, without Pico hardware.
#include <string.h>
#include <math.h>
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "lsm6dsv.h"

static uint8_t regs[2][128], bank, fifo_record[7];
static uint8_t queue[32][7];
static unsigned queue_size, queue_index;
static unsigned writes, highg_writes, reset_polls, sleeps, transient_reads;
static bool reset_stuck, reject_config, short_transfer, short_fifo, reject_highg;

void gpio_put(uint p, bool v) { (void)p; (void)v; }
void gpio_init(uint p) { (void)p; }
void gpio_set_dir(uint p, bool v) { (void)p; (void)v; }
void gpio_set_function(uint p, uint f) { (void)p; (void)f; }
void gpio_set_irq_enabled(uint p, uint32_t e, bool v) { (void)p; (void)e; (void)v; }
void gpio_set_irq_enabled_with_callback(uint p, uint32_t e, bool v,
                                       void (*cb)(uint,uint32_t)) {
    (void)p; (void)e; (void)v; (void)cb;
}
void sleep_ms(unsigned ms) { sleeps += ms; }
void spi_init(int s, unsigned b) { (void)s; (void)b; }
void spi_set_format(int s, unsigned b, unsigned p, unsigned h, unsigned o) {
    (void)s; (void)b; (void)p; (void)h; (void)o;
}
int spi_write_blocking(int s, const uint8_t *tx, size_t n) {
    (void)s;
    if (n != 2) return 0;
    uint8_t addr = tx[0] & 127, value = tx[1];
    ++writes;
    if (addr == 1) { bank = value >> 7; return (int)n; }
    if (!bank && addr == 0x0a && value == 0) {
        queue_size = queue_index = 0;
        regs[0][0x1b] = regs[0][0x1c] = 0;
    }
    if (!bank && addr == 0x4e) ++highg_writes;
    if (!bank && addr == 0x12 && (value & 1)) {
        uint8_t id = regs[0][0x0f];
        memset(regs, 0, sizeof regs);
        regs[0][0x0f] = id;
        regs[0][0x12] = 1;
        reset_polls = 0;
    } else if (!(reject_config && !bank && addr == 0x10) &&
               !(reject_highg && !bank && addr == 0x4e)) {
        regs[bank][addr] = value;
    }
    return (int)n;
}
int spi_write_read_blocking(int s, const uint8_t *tx, uint8_t *rx, size_t n) {
    (void)s;
    if (short_transfer) return 0;
    memset(rx, 0, n);
    uint8_t addr = tx[0] & 127;
    if (short_fifo && addr == 0x78) return 0;
    if (!bank && addr == 0x0f && transient_reads) {
        --transient_reads;
        return (int)n;
    }
    if (!bank && addr == 0x12 && (regs[0][0x12] & 1) &&
        !reset_stuck && ++reset_polls >= 3) regs[0][0x12] = 4;
    if (!bank && addr == 0x78 && n == 8) {
        if (queue_index < queue_size) {
            memcpy(rx + 1, queue[queue_index++], 7);
            regs[0][0x1b] = (uint8_t)(queue_size - queue_index);
        } else { memcpy(rx + 1, fifo_record, 7); regs[0][0x1b] = 0; }
    } else {
        for (size_t i = 1; i < n; ++i) rx[i] = regs[bank][addr + i - 1];
    }
    return (int)n;
}
static void device(uint8_t id) {
    memset(regs, 0, sizeof regs);
    memset(fifo_record, 0, sizeof fifo_record);
    regs[0][0x0f] = id;
    bank = 0;
    queue_size = queue_index = 0;
    writes = highg_writes = reset_polls = sleeps = transient_reads = 0;
    reset_stuck = reject_config = short_transfer = short_fifo = reject_highg = false;
}
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static void enqueue(uint8_t tag, uint8_t slot, int16_t x) {
    if (queue_index == queue_size) queue_size = queue_index = 0;
    uint8_t *r = queue[queue_size++];
    memset(r,0,7);
    r[0] = (uint8_t)((tag << 3) | (slot << 1));
    r[1] = (uint8_t)x; r[2] = (uint8_t)((uint16_t)x >> 8);
    regs[0][0x1b] = (uint8_t)(queue_size - queue_index);
}

static void rotation_record(uint8_t slot, uint16_t first, uint16_t second, uint16_t third) {
    enqueue(0x13,slot,(int16_t)first);
    uint8_t *r=queue[queue_size-1];
    r[3]=(uint8_t)second; r[4]=(uint8_t)(second>>8);
    r[5]=(uint8_t)third; r[6]=(uint8_t)(third>>8);
}

int imu_driver_test(void) {
    for (unsigned model = 0; model < 2; ++model) {
        device(model ? 0x73 : 0x70);
        transient_reads = 2;
        CHECK(lsm6dsv_init());
        CHECK(lsm6dsv_device_id() == (model ? 0x73 : 0x70));
        CHECK(lsm6dsv_model() == (model ? LSM6DSV_MODEL_LSM6DSV320X : LSM6DSV_MODEL_LSM6DSV));
        CHECK(strcmp(lsm6dsv_model_name(), model ? "LSM6DSV320X" : "LSM6DSV") == 0);
        CHECK(highg_writes == model);
        CHECK(regs[0][0x10] == 6 && regs[0][0x17] == 0 && regs[0][0x12] == 0x44);
        CHECK(sleeps >= 60);
        regs[0][0x28] = 0; regs[0][0x29] = 0x40; // +1g at +/-2g
        regs[0][0x2a] = 0; regs[0][0x2b] = 0xc0; // -1g
        int16_t x, y, z;
        CHECK(lsm6dsv_read_accel(&x,&y,&z) && x == 16384 && y == -16384 && z == 0);
        short_transfer = true;
        CHECK(!lsm6dsv_read_accel(&x,&y,&z));
        short_transfer = false;
        CHECK(lsm6dsv_gyro_start());
        CHECK(regs[0][0x11] == 7 && regs[0][0x15] == 4);
        CHECK(!lsm6dsv_read_gyro(&x,&y,&z));
        regs[0][0x1e] = 2; regs[0][0x23] = 1;
        CHECK(lsm6dsv_read_gyro(&x,&y,&z) && x == 256);
        lsm6dsv_gyro_stop();
        CHECK(regs[0][0x11] == 0);
        CHECK(lsm6dsv_fifo_start());
        CHECK(regs[0][0x10] == 11 && regs[0][0x17] == 3 && regs[0][9] == 11);
        lsm6dsv_accel_sample_t sample;
        bool overrun = false;
        regs[0][0x1b] = 1; fifo_record[0] = 2 << 3; fifo_record[2] = 8;
        CHECK(lsm6dsv_fifo_read(&sample,1,&overrun) == 1 && sample.x == 2048);
        regs[0][0x1b] = 1; fifo_record[0] = 1 << 3; // gyro cannot become acceleration
        CHECK(lsm6dsv_fifo_read(&sample,1,&overrun) == 0);
        regs[0][0x1c] = 0x40;
        CHECK(lsm6dsv_fifo_read(&sample,1,&overrun) == 0 && overrun);
        regs[0][0x1c] = 0;
        lsm6dsv_fifo_stop();
        CHECK(regs[0][0x17] == 0 && regs[0][0x10] == 6);
        CHECK(lsm6dsv_beam_fifo_start());
        CHECK(regs[0][0x10] == 8 && regs[0][0x17] == 1 && regs[0][9] == 8);
        lsm6dsv_fifo_stop();
        CHECK(lsm6dsv_orientation_start() && bank == 0);
        CHECK(regs[1][4] == 2 && regs[1][0x44] == 2 && regs[1][0x5e] == 0x10);
        lsm6dsv_quaternion_t q;
        if (!model) {
            rotation_record(0,0x3800,0x3800,0x3800); // legacy xyz = .5 each
            CHECK(lsm6dsv_orientation_read(&q,&overrun));
            CHECK(fabsf(q.w-.5f)<.0001f && fabsf(q.x-.5f)<.0001f && fabsf(q.z-.5f)<.0001f);
        } else {
            rotation_record(0,1,0,0x3c00); // orphan ZW cannot produce a quaternion
            CHECK(!lsm6dsv_orientation_read(&q,&overrun));
            rotation_record(1,0,0x3800,0x3800);
            CHECK(!lsm6dsv_orientation_read(&q,&overrun)); // XY split across calls
            rotation_record(1,1,0x3800,0xb800); // z=.5, w=-.5: preserve w's sign
            CHECK(lsm6dsv_orientation_read(&q,&overrun));
            CHECK(fabsf(q.w+.5f)<.0001f && fabsf(q.x-.5f)<.0001f && fabsf(q.y-.5f)<.0001f && fabsf(q.z-.5f)<.0001f);
            rotation_record(2,0,0,0); rotation_record(3,1,0,0x3c00);
            CHECK(!lsm6dsv_orientation_read(&q,&overrun)); // mismatched time slots
            rotation_record(3,0,0x7e00,0); rotation_record(3,1,0,0x3c00);
            CHECK(!lsm6dsv_orientation_read(&q,&overrun)); // NaN
            rotation_record(0,0,0x3c00,0x3c00); rotation_record(0,1,0x3c00,0x3c00);
            CHECK(!lsm6dsv_orientation_read(&q,&overrun)); // invalid norm
            rotation_record(1,0,0,0); enqueue(2,1,0); rotation_record(1,1,0,0x3c00);
            CHECK(!lsm6dsv_orientation_read(&q,&overrun)); // interruption invalidates pending XY
            rotation_record(2,0,0,0);
            CHECK(!lsm6dsv_orientation_read(&q,&overrun));
            regs[0][0x1c]=0x40;
            CHECK(!lsm6dsv_orientation_read(&q,&overrun) && overrun);
            regs[0][0x1c]=0;
            rotation_record(2,1,0,0x3c00);
            CHECK(!lsm6dsv_orientation_read(&q,&overrun));
            rotation_record(3,0,0,0); rotation_record(3,1,0,0x3c00);
            CHECK(lsm6dsv_orientation_read(&q,&overrun) && q.w==1 && q.x==0);
        }
        lsm6dsv_orientation_stop();
        CHECK(bank == 0 && regs[0][0x10] == 6 && regs[0][0x17] == 0);
        CHECK(regs[0][0x4e] == 0 && !(regs[0][0x0b] & 8));
        if (!model) CHECK(highg_writes == 0); // never touch v2-only registers on v1
        CHECK(lsm6dsv_drop_fifo_start());
        CHECK(lsm6dsv_dual_fifo_active() == (bool)model);
        if (model) {
            CHECK(regs[0][0x4e] == 0x34 && (regs[0][0x0b] & 8));
            lsm6dsv_accel_pair_t p[3];
            enqueue(0x02,0,111); // startup partial is discarded, not paired to another slot
            CHECK(lsm6dsv_dual_fifo_read(p,3) == 0);
            enqueue(0x1d,1,-96); enqueue(0x02,1,2048);
            enqueue(0x02,2,2049); enqueue(0x1d,2,-97);
            CHECK(lsm6dsv_dual_fifo_read(p,1) == 1 && p[0].low.x == 2048 && p[0].high.x == -96);
            CHECK(lsm6dsv_dual_fifo_read(p,3) == 1 && p[0].low.x == 2049 && p[0].high.x == -97);
            enqueue(0x02,3,-2048);
            CHECK(lsm6dsv_dual_fifo_read(p,3) == 0); // partial across calls
            enqueue(0x1d,3,96); enqueue(0x1d,0,97); enqueue(0x02,0,-2049);
            CHECK(lsm6dsv_dual_fifo_read(p,3) == 2 && p[0].low.x == -2048 && p[1].low.x == -2049);
            lsm6dsv_dual_stats_t st = lsm6dsv_dual_fifo_stats();
            CHECK(st.pairs == 4 && st.startup_discarded == 1 && !st.sequence_errors);
            enqueue(0x02,2,0); // skipped slot 1 must fail rather than compress time
            CHECK(lsm6dsv_dual_fifo_read(p,3) == 0 && lsm6dsv_dual_fifo_stats().sequence_errors == 1);
            lsm6dsv_fifo_stop();
            CHECK(!lsm6dsv_dual_fifo_active() && regs[0][0x4e] == 0 && !(regs[0][0x0b] & 8));
            CHECK(lsm6dsv_drop_fifo_start());
            enqueue(0x02,0,1); enqueue(0x02,0,2); // duplicate low record
            CHECK(lsm6dsv_dual_fifo_read(p,3) == 0 && lsm6dsv_dual_fifo_stats().sequence_errors == 1);
            lsm6dsv_fifo_stop();
            CHECK(lsm6dsv_drop_fifo_start());
            enqueue(0x01,0,0); // unexpected gyro tag is an integrity failure
            CHECK(lsm6dsv_dual_fifo_read(p,3) == 0 && lsm6dsv_dual_fifo_stats().unexpected_tags == 1);
            lsm6dsv_fifo_stop();
            CHECK(lsm6dsv_drop_fifo_start());
            regs[0][0x1c] = 0x40;
            CHECK(lsm6dsv_dual_fifo_read(p,3) == 0 && lsm6dsv_dual_fifo_stats().overrun);
            regs[0][0x1c] = 0; lsm6dsv_fifo_stop();
            CHECK(lsm6dsv_drop_fifo_start());
            enqueue(0x02,0,0); short_fifo = true;
            CHECK(lsm6dsv_dual_fifo_read(p,3) == 0 && lsm6dsv_dual_fifo_stats().io_errors == 1);
            short_fifo = false;
            lsm6dsv_fifo_stop();
            reject_highg = true;
            CHECK(!lsm6dsv_drop_fifo_start() && !lsm6dsv_dual_fifo_active());
            reject_highg = false;
            CHECK(lsm6dsv_beam_fifo_start() && !lsm6dsv_dual_fifo_active());
        }
        lsm6dsv_fifo_stop();
    }
    const uint8_t invalid[] = {0, 0xff, 0x71};
    for (unsigned i = 0; i < sizeof invalid; ++i) {
        device(invalid[i]);
        CHECK(!lsm6dsv_init() && lsm6dsv_device_id() == invalid[i]);
        CHECK(lsm6dsv_model() == LSM6DSV_MODEL_UNKNOWN && writes == 0);
        CHECK(!lsm6dsv_fifo_start() && !lsm6dsv_beam_fifo_start());
        CHECK(!lsm6dsv_gyro_start() && !lsm6dsv_orientation_start());
        lsm6dsv_fifo_stop(); lsm6dsv_gyro_stop();
        CHECK(writes == 0);
    }
    device(0x73); reset_stuck = true;
    CHECK(!lsm6dsv_init() && sleeps < 200 && lsm6dsv_model() == LSM6DSV_MODEL_UNKNOWN);
    device(0x73); reject_config = true;
    CHECK(!lsm6dsv_init() && lsm6dsv_model() == LSM6DSV_MODEL_UNKNOWN);
    device(0x70);
    CHECK(lsm6dsv_init()); // recovery after failed detection/configuration
    return 0;
}
