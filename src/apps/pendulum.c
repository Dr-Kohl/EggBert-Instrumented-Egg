#include <stdio.h>
#include <string.h>
#include <math.h>
#include "app.h"
#include "ui.h"
#include "calibration.h"
#include "pendulum_detector.h"
#include "pico/stdlib.h"

#define PENDULUM_ARM_DELAY_MS   2500u
typedef enum {
    PENDULUM_READY,
    PENDULUM_WAITING,
    PENDULUM_MEASURING,
    PENDULUM_RESULT,
    PENDULUM_WEAK,
} pendulum_state_t;

static pendulum_state_t pendulum_state;
static pendulum_detector_t pendulum_detector;
static absolute_time_t pendulum_arm_at;

static void pendulum_arm(void) {
    pendulum_state = PENDULUM_WAITING;
    pendulum_arm_at = make_timeout_time_ms(PENDULUM_ARM_DELAY_MS);
    pendulum_detector_reset(&pendulum_detector);
}

static void pendulum_process_sample(void) {
    if (pendulum_state == PENDULUM_WAITING) {
        if (!time_reached(pendulum_arm_at)) return;
        pendulum_state = PENDULUM_MEASURING;
    }
    if (pendulum_state != PENDULUM_MEASURING) return;

    // Radial acceleration peaks at both bottom crossings of a full cycle.
    // Convert calibrated live counts to g; the detector returns the full period.
    pendulum_detector_sample(&pendulum_detector,
        (float)app_sample_current()->corrected[0] / LIVE_ACCEL_COUNTS_PER_G,
        to_ms_since_boot(get_absolute_time()));
    if (pendulum_detector.complete) pendulum_state = PENDULUM_RESULT;
    else if (pendulum_detector.weak) pendulum_state = PENDULUM_WEAK;
}

static void show_pendulum(void) {
    if (!ui_display_available()) return;
    char line[12];
    ui_begin("PENDULUM");
    if (pendulum_state == PENDULUM_READY) {
        ssd1306_ui_text(2, 32, "HOLD UP", true);
        ssd1306_ui_text(2, 46, "MID ARM", true);
        ssd1306_ui_text(2, 112, "UP HOME", true);
    } else if (pendulum_state == PENDULUM_WAITING) {
        ssd1306_ui_text(2, 32, "HOLD", true);
        ssd1306_ui_text(2, 47, "AUTO ARM", true);
        ssd1306_ui_text(2, 112, "UP HOME", true);
    } else if (pendulum_state == PENDULUM_MEASURING) {
        ssd1306_ui_text(2, 32, "RELEASE", true);
        snprintf(line, sizeof line, "PASS %u/6", (unsigned)pendulum_detector.interval_count);
        ssd1306_ui_text(2, 47, line, true);
        ssd1306_ui_text(2, 62, "MID STOP", true);
    } else if (pendulum_state == PENDULUM_WEAK) {
        ssd1306_ui_text(2, 32, "TRY AGAIN", true);
        ssd1306_ui_text(2, 47, "NO RESULT", true);
        ssd1306_ui_text(2, 62, "MID AGAIN", true);
        ssd1306_ui_text(2, 112, "DN EXIT", true);
    } else {
        float period_seconds = pendulum_detector.period_seconds;
        float length_cm = 980.665f * (period_seconds / 6.2831853f) *
                          (period_seconds / 6.2831853f);
        uint32_t period_hundredths = (uint32_t)(period_seconds * 100.0f + 0.5f);
        uint32_t length_tenths = (uint32_t)(length_cm * 10.0f + 0.5f);
        snprintf(line, sizeof line, "PER %lu.%02lu",
                 (unsigned long)(period_hundredths / 100u),
                 (unsigned long)(period_hundredths % 100u));
        ssd1306_ui_text(2, 32, line, true);
        ssd1306_ui_text(2, 44, "SECONDS", true);
        snprintf(line, sizeof line, "LEN %lu.%lu",
                 (unsigned long)(length_tenths / 10u),
                 (unsigned long)(length_tenths % 10u));
        ssd1306_ui_text(2, 64, line, true);
        ssd1306_ui_text(2, 76, "CM", true);
        ssd1306_ui_text(2, 94, "MID AGAIN", true);
        ssd1306_ui_text(2, 112, "DN EXIT", true);
    }
    ssd1306_show();
}
static void pendulum_button(unsigned button) {
    if (button == 1) {
        if (pendulum_state == PENDULUM_READY || pendulum_state == PENDULUM_RESULT || pendulum_state == PENDULUM_WEAK) pendulum_arm();
        else pendulum_state = PENDULUM_READY;
    } else if (button == 2 && (pendulum_state == PENDULUM_RESULT || pendulum_state == PENDULUM_WEAK)) {
        app_return_home();
    } else if (button == 0 && pendulum_state != PENDULUM_MEASURING) {
        app_return_home();
    }
    return;
}

static void pendulum_enter(void) { pendulum_state=PENDULUM_READY; }
static void pendulum_sample(const AppSample *sample) { (void)sample; pendulum_process_sample(); show_pendulum(); }
static uint32_t pendulum_period(void) { return pendulum_state!=PENDULUM_READY ? 20 : 250; }
static bool pendulum_can_idle(void) { return pendulum_state != PENDULUM_WAITING && pendulum_state != PENDULUM_MEASURING; }
const App pendulum_app = {.can_idle=pendulum_can_idle,.enter=pendulum_enter,.draw=show_pendulum,.button=pendulum_button,
    .sample=pendulum_sample,.sample_period_ms=pendulum_period};
