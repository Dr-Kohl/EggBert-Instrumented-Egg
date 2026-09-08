#include <stdio.h>

#include "board.h"
#include "hardware/gpio.h"
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

int main(void) {
    stdio_init_all();
    setup_gpio();
    sleep_ms(1500);
    printf("EggBert RP2354A bring-up starting\n");

    for (unsigned i = 0; i < 3; ++i) { leds(1u << i); sleep_ms(300); }
    leds(0);

    bool oled_ok = ssd1306_init();
    printf("OLED at 0x%02X: %s\n", OLED_I2C_ADDRESS, oled_ok ? "OK" : "NOT FOUND");
    if (oled_ok) {
        ssd1306_text(0, "E G G B E R T");
        ssd1306_text(2, "R P 2 3 5 4 A");
        ssd1306_text(4, "B R I N G U P");
        ssd1306_show();
    }

    unsigned previous = 0;
    while (true) {
        unsigned pressed = 0;
        for (unsigned i = 0; i < 3; ++i)
            if (!gpio_get(BUTTONS[i])) pressed |= 1u << i;
        leds(pressed);
        if (pressed != previous) {
            printf("Buttons: SW5=%u SW2=%u SW3=%u\n", pressed & 1u, (pressed >> 1) & 1u, (pressed >> 2) & 1u);
            previous = pressed;
        }
        sleep_ms(10);
    }
}
