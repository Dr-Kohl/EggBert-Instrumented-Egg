#include <stdio.h>
#include <string.h>
#include <math.h>
#include "app.h"
#include "ui.h"
#include "calibration.h"
#include "pico/stdlib.h"

static absolute_time_t next_level_display;

static void format_tilt(char *text, size_t text_size, char axis, float degrees) {
    // Keep formatting integer-only: the compact Pico printf configuration need
    // not enable floating-point conversion just for this display.
    int32_t tenths = (int32_t)(degrees * 10.0f + (degrees >= 0.0f ? 0.5f : -0.5f));
    char sign = tenths < 0 ? '-' : '+';
    uint32_t magnitude = (uint32_t)(tenths < 0 ? -tenths : tenths);
    snprintf(text, text_size, "%c%c%lu.%lu", axis, sign,
             (unsigned long)(magnitude / 10u), (unsigned long)(magnitude % 10u));
}

static void show_inclinometer(void) {
    if (!ui_display_available()) return;
    const float radians_to_degrees = 57.2957795f;
    // A positive X (or Y) acceleration produces a positive displayed X (or
    // Y) tilt. The other horizontal axis is included to keep the result
    // accurate even when EggBert is not level in that direction.
    float x_degrees = atan2f(app_sample_current()->filtered[0],
                             sqrtf(app_sample_current()->filtered[1] * app_sample_current()->filtered[1] +
                                   app_sample_current()->filtered[2] * app_sample_current()->filtered[2])) * radians_to_degrees;
    float y_degrees = atan2f(app_sample_current()->filtered[1],
                             sqrtf(app_sample_current()->filtered[0] * app_sample_current()->filtered[0] +
                                   app_sample_current()->filtered[2] * app_sample_current()->filtered[2])) * radians_to_degrees;
    char x_text[8], y_text[8];
    format_tilt(x_text, sizeof x_text, 'X', x_degrees);
    format_tilt(y_text, sizeof y_text, 'Y', y_degrees);
    ui_begin("LEVEL");
    ssd1306_ui_text(2, 26, "DEGREES", true);
    ssd1306_ui_text_double_height(2, 36, x_text, true);
    ssd1306_ui_text_double_height(2, 62, y_text, true);
    ssd1306_ui_text(2, 112, "MID EXIT", true);
    ssd1306_show();
}

static void level_button(unsigned button) { if (button==1) app_return_home(); }
static void level_sample(const AppSample *sample) {
    (void)sample;
    if (time_reached(next_level_display)) {
        show_inclinometer(); next_level_display=make_timeout_time_ms(200);
    }
}
static uint32_t level_period(void) { return 100; }
static bool level_can_idle(void) { return true; }
const App level_app = {.can_idle=level_can_idle,.draw=show_inclinometer,.button=level_button,.sample=level_sample,.sample_period_ms=level_period};
