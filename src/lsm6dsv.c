#include "lsm6dsv.h"

#include "board.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

#define LSM6DSV_WHO_AM_I       0x0Fu
#define LSM6DSV_WHO_AM_I_VALUE 0x70u
#define LSM6DSV_CTRL1          0x10u
#define LSM6DSV_OUTX_L_A       0x28u

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
