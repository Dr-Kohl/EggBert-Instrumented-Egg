#include <stdio.h>
#include <string.h>
#include <math.h>
#include "app.h"
#include "ui.h"
#include "calibration.h"
#include "pico/stdlib.h"

static void format_acceleration(char *text, size_t text_size, char axis, int32_t counts) {
    // The live display uses the calibrated +/-2 g scale: 16,384 counts per g.
    // Keep this integer-only, matching the compact Pico printf configuration.
    int32_t hundredths = (counts * 100 + (counts >= 0 ?
        (int32_t)(LIVE_ACCEL_COUNTS_PER_G / 2u) : -(int32_t)(LIVE_ACCEL_COUNTS_PER_G / 2u))) /
        (int32_t)LIVE_ACCEL_COUNTS_PER_G;
    char sign = hundredths < 0 ? '-' : '+';
    uint32_t magnitude = (uint32_t)(hundredths < 0 ? -hundredths : hundredths);
    if (axis == 'M') {
        snprintf(text, text_size, "MAG %lu.%02lug",
                 (unsigned long)(magnitude / 100u), (unsigned long)(magnitude % 100u));
        return;
    }
    snprintf(text, text_size, "%c %c%lu.%02lug", axis, sign,
             (unsigned long)(magnitude / 100u), (unsigned long)(magnitude % 100u));
}

static void show_acceleration(void) {
    if (!ui_display_available()) return;
    char text[13];
    ui_begin("ACCEL G");
    format_acceleration(text, sizeof text, 'X', app_sample_current()->corrected[0]);
    ssd1306_ui_text(2, 30, text, true);
    format_acceleration(text, sizeof text, 'Y', app_sample_current()->corrected[1]);
    ssd1306_ui_text(2, 44, text, true);
    format_acceleration(text, sizeof text, 'Z', app_sample_current()->corrected[2]);
    ssd1306_ui_text(2, 60, text, true);
    int32_t magnitude = (int32_t)sqrtf((float)app_sample_current()->corrected[0] * app_sample_current()->corrected[0] +
                                       (float)app_sample_current()->corrected[1] * app_sample_current()->corrected[1] +
                                       (float)app_sample_current()->corrected[2] * app_sample_current()->corrected[2]);
    format_acceleration(text, sizeof text, 'M', magnitude);
    ssd1306_ui_text(2, 82, text, true);
    ssd1306_ui_text(2, 112, "MID EXIT", true);
    ssd1306_show();
}

static void acceleration_button(unsigned button) { if (button==1) app_return_home(); }
static void acceleration_sample(const AppSample *sample) { (void)sample; show_acceleration(); }
static uint32_t acceleration_period(void) { return 100; }
const App acceleration_app = {.draw=show_acceleration,.button=acceleration_button,
    .sample=acceleration_sample,.sample_period_ms=acceleration_period};
