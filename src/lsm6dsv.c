#include "lsm6dsv.h"

#include "board.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

#define LSM6DSV_WHO_AM_I       0x0Fu
#define LSM6DSV_WHO_AM_I_VALUE 0x70u
#define LSM6DSV_CTRL1          0x10u
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

    sample->x = (int16_t)((uint16_t)rx[2] | ((uint16_t)rx[3] << 8));
    sample->y = (int16_t)((uint16_t)rx[4] | ((uint16_t)rx[5] << 8));
    sample->z = (int16_t)((uint16_t)rx[6] | ((uint16_t)rx[7] << 8));
    return true;
}

bool lsm6dsv_init(void) {
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

    if (read_reg(LSM6DSV_WHO_AM_I) != LSM6DSV_WHO_AM_I_VALUE) return false;
    gpio_set_irq_enabled_with_callback(IMU_INT1_PIN, GPIO_IRQ_EDGE_RISE, false,
                                       &imu_irq_callback);
    // High-performance mode, +/-2 g (default scale), 120 Hz output data rate.
    write_reg(LSM6DSV_CTRL1, 0x06u);
    return true;
}

bool lsm6dsv_read_accel(int16_t *x, int16_t *y, int16_t *z) {
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

bool lsm6dsv_fifo_start(void) {
    // Reset the FIFO, then configure it before enabling the high-rate sensor.
    write_reg(LSM6DSV_FIFO_CTRL4, 0x00u); // bypass mode
    write_reg(LSM6DSV_INT1_CTRL, 0x00u);
    write_reg(LSM6DSV_FIFO_CTRL1, LSM6DSV_FIFO_WATERMARK_SAMPLES);
    write_reg(LSM6DSV_FIFO_CTRL2, 0x00u);
    write_reg(LSM6DSV_FIFO_CTRL3, 0x0Bu); // accelerometer FIFO batch rate: 3.84 kHz
    write_reg(LSM6DSV_CTRL8, 0x03u);      // accelerometer: +/-16 g
    write_reg(LSM6DSV_CTRL1, 0x0Bu);      // accelerometer ODR: 3.84 kHz, high-performance
    write_reg(LSM6DSV_INT1_CTRL, 0x18u);  // FIFO watermark and FIFO overrun
    fifo_irq_pending = false;
    gpio_set_irq_enabled(IMU_INT1_PIN, GPIO_IRQ_EDGE_RISE, true);
    write_reg(LSM6DSV_FIFO_CTRL4, 0x06u); // continuous FIFO mode
    return true;
}

void lsm6dsv_fifo_stop(void) {
    gpio_set_irq_enabled(IMU_INT1_PIN, GPIO_IRQ_EDGE_RISE, false);
    write_reg(LSM6DSV_INT1_CTRL, 0x00u);
    write_reg(LSM6DSV_FIFO_CTRL4, 0x00u); // bypass / discard unread FIFO data
    write_reg(LSM6DSV_CTRL8, 0x00u);      // restore +/-2 g
    write_reg(LSM6DSV_CTRL1, 0x06u);      // restore 120 Hz live readout
    fifo_irq_pending = false;
}

size_t lsm6dsv_fifo_read(lsm6dsv_accel_sample_t *samples, size_t max_samples,
                         bool *overrun) {
    if (!samples || max_samples == 0) return 0;
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
