#include <stdio.h>

#include "board.h"
#include "hardware/gpio.h"
#include "lsm6dsv.h"
#include "pico/stdlib.h"
#include "ssd1306.h"

static const uint LEDS[] = {LED_GREEN_PIN, LED_YELLOW_PIN, LED_RED_PIN};
static const uint BUTTONS[] = {BUTTON_1_PIN, BUTTON_2_PIN, BUTTON_3_PIN};

static void setup_gpio(void) {
    for (unsigned i = 0; i < 3; ++i) {
        gpio_init(LEDS[i]); gpio_set_dir(LEDS[i], GPIO_OUT); gpio_put(LEDS[i], false);
        gpio_init(BUTTONS[i]); gpio_set_dir(BUTTONS[i], GPIO_IN); gpio_pull_up(BUTTONS[i]);
    }
}

static void leds(unsigned mask) {
    for (unsigned i = 0; i < 3; ++i) gpio_put(LEDS[i], (mask >> i) & 1u);
}

static void show_accel(bool oled_ok, int16_t x, int16_t y, int16_t z) {
    if (!oled_ok) return;
    char line[22];
    ssd1306_clear();
    ssd1306_text(0, "A C C E L");
    snprintf(line, sizeof line, "X %d", x); ssd1306_text(2, line);
    snprintf(line, sizeof line, "Y %d", y); ssd1306_text(4, line);
    snprintf(line, sizeof line, "Z %d", z); ssd1306_text(6, line);
    ssd1306_show();
}

int main(void) {
    stdio_init_all();
    setup_gpio();
    sleep_ms(1500);
    printf("EggBert RP2354A bring-up starting\n");

    for (unsigned i = 0; i < 3; ++i) { leds(1u << i); sleep_ms(300); }
    leds(0);

    bool oled_ok = ssd1306_init();
    printf("OLED at 0x%02X: %s\n", OLED_I2C_ADDRESS, oled_ok ? "OK" : "NOT FOUND");
    bool imu_ok = lsm6dsv_init();
    printf("LSM6DSV SPI WHO_AM_I: %s\n", imu_ok ? "0x70 OK" : "NOT FOUND");
    if (oled_ok) {
        ssd1306_clear();
        ssd1306_text(0, "E G G B E R T");
        ssd1306_text(2, imu_ok ? "I M U O K" : "I M U F A I L");
        ssd1306_show();
    }

    unsigned previous = 0;
    absolute_time_t next_imu_report = make_timeout_time_ms(250);
    while (true) {
        unsigned pressed = 0;
        for (unsigned i = 0; i < 3; ++i)
            if (!gpio_get(BUTTONS[i])) pressed |= 1u << i;
        leds(pressed);
        if (pressed != previous) {
            printf("Buttons: SW5=%u SW2=%u SW3=%u\n", pressed & 1u, (pressed >> 1) & 1u, (pressed >> 2) & 1u);
            previous = pressed;
        }
        if (imu_ok && absolute_time_diff_us(get_absolute_time(), next_imu_report) <= 0) {
            int16_t x, y, z;
            if (lsm6dsv_read_accel(&x, &y, &z)) {
                printf("Accel raw: X=%d Y=%d Z=%d (0.061 mg/LSB)\n", x, y, z);
                show_accel(oled_ok, x, y, z);
            }
            next_imu_report = make_timeout_time_ms(250);
        }
        sleep_ms(10);
    }
}
