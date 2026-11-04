#include <stdio.h>
#include <string.h>
#include <math.h>
#include "app.h"
#include "ui.h"
#include "calibration.h"
#include "pico/stdlib.h"

static void show_raw_xyz(void) {
    if (!ui_display_available()) return;
    char raw[8];
    ui_begin("RAW XYZ");
    ssd1306_ui_text(2, 30, "X", true);
    snprintf(raw, sizeof raw, "%d", app_sample_current()->corrected[0]);
    ssd1306_ui_text(14, 30, raw, true);
    ssd1306_ui_text(2, 44, "Y", true);
    snprintf(raw, sizeof raw, "%d", app_sample_current()->corrected[1]);
    ssd1306_ui_text(14, 44, raw, true);
    ssd1306_ui_text(2, 60, "Z", true);
    snprintf(raw, sizeof raw, "%d", app_sample_current()->corrected[2]);
    ssd1306_ui_text(14, 60, raw, true);
    ssd1306_ui_text(2, 112, "MID EXIT", true);
    ssd1306_show();
}

static void raw_button(unsigned button) { if (button==1) app_return_home(); }
static void raw_sample(const AppSample *sample) { (void)sample; show_raw_xyz(); }
static uint32_t raw_period(void) { return 100; }
const App raw_app = {.draw=show_raw_xyz,.button=raw_button,.sample=raw_sample,.sample_period_ms=raw_period};
