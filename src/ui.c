#include <stdio.h>
#include "ui.h"
#include "lsm6dsv.h"
static bool display_available, usb_power_present;
static uint16_t power_out_millivolts;
bool ui_display_available(void) { return display_available; }
void ui_set_display_available(bool available) { display_available=available; }
void ui_set_power(uint16_t millivolts, bool usb) { power_out_millivolts=millivolts; usb_power_present=usb; }
void ui_draw_status_header(void) {
    // Common full-width layout for white and yellow/blue panels. Power is
    // shown independently of whether a computer/viewer is connected.
    if (usb_power_present) {
        ssd1306_ui_fill_rect(3, 3, 7, 4, true);
        ssd1306_ui_fill_rect(4, 1, 1, 2, true);
        ssd1306_ui_fill_rect(8, 1, 1, 2, true);
        ssd1306_ui_fill_rect(6, 7, 1, 2, true);
        ssd1306_ui_text(14, 1, "USB", true);
    } else {
        ssd1306_ui_fill_rect(2, 2, 9, 6, true);
        ssd1306_ui_fill_rect(3, 3, 7, 4, false);
        ssd1306_ui_fill_rect(11, 4, 1, 2, true);
        unsigned bars = power_out_millivolts >= 4050u ? 3u :
                        power_out_millivolts >= 3850u ? 2u :
                        power_out_millivolts >= 3650u ? 1u : 0u;
        for (unsigned bar=0; bar<bars; ++bar)
            ssd1306_ui_fill_rect((uint8_t)(4+bar*2u), 4, 1, 2, true);
        char volts[8];
        unsigned tenths = (power_out_millivolts+50u)/100u;
        snprintf(volts, sizeof volts, "%u.%uV", tenths/10u, tenths%10u);
        ssd1306_ui_text(14, 1, volts, true);
    }
    ssd1306_ui_text(50, 1, lsm6dsv_model()==LSM6DSV_MODEL_LSM6DSV320X ? "V2" :
                   lsm6dsv_model()==LSM6DSV_MODEL_LSM6DSV ? "V1" : "V-", true);
    ssd1306_ui_fill_rect(0, 11, 64, 1, true);
}

void ui_begin(const char *title) {
    ssd1306_clear();
    ui_draw_status_header();
    ssd1306_ui_text(2, 16, title, true);
}

void ui_menu_item(uint8_t y, const char *text, bool selected) {
    if (selected) ssd1306_ui_fill_rect(0, (uint8_t)(y-1u), 64, 10, true);
    ssd1306_ui_text(2, y, text, !selected);
}
