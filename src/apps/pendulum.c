#include <stdio.h>
#include <string.h>
#include <math.h>
#include "app.h"
#include "ui.h"
#include "calibration.h"
#include "pico/stdlib.h"

#define PENDULUM_ARM_DELAY_MS   2500u
#define PENDULUM_PEAK_EXCESS_COUNTS (LIVE_ACCEL_COUNTS_PER_G * 5u / 1000u)
#define PENDULUM_MIN_PERIOD_MS   300u
#define PENDULUM_MAX_PERIOD_MS  8000u
#define PENDULUM_PERIODS_TO_AVERAGE 4u
typedef enum {
    PENDULUM_READY,
    PENDULUM_WAITING,
    PENDULUM_MEASURING,
    PENDULUM_RESULT,
} pendulum_state_t;

static pendulum_state_t pendulum_state;
static float pendulum_previous_signal;
static float pendulum_current_signal;
static float pendulum_minimum_since_peak;
static bool pendulum_signal_valid;
static uint32_t pendulum_last_peak_ms;
static uint32_t pendulum_period_total_ms;
static uint8_t pendulum_period_count;
static absolute_time_t pendulum_arm_at;

static void pendulum_arm(void) {
    pendulum_state = PENDULUM_WAITING;
    pendulum_arm_at = make_timeout_time_ms(PENDULUM_ARM_DELAY_MS);
    pendulum_signal_valid = false;
    pendulum_last_peak_ms = 0;
    pendulum_period_total_ms = 0;
    pendulum_period_count = 0;
}

static void pendulum_process_sample(void) {
    if (pendulum_state == PENDULUM_WAITING) {
        if (!time_reached(pendulum_arm_at)) return;
        pendulum_state = PENDULUM_MEASURING;
        pendulum_signal_valid = false;
    }
    if (pendulum_state != PENDULUM_MEASURING) return;

    // The hook is on EggBert's -X side, so |X| is the radial proper
    // acceleration. It peaks once per full swing at the bottom of the arc.
    float raw_signal = fabsf((float)app_sample_current()->corrected[0]);
    if (!pendulum_signal_valid) {
        pendulum_previous_signal = raw_signal;
        pendulum_current_signal = raw_signal;
        pendulum_minimum_since_peak = raw_signal;
        pendulum_signal_valid = true;
        return;
    }
    float next_signal = 0.70f * pendulum_current_signal + 0.30f * raw_signal;
    if (next_signal < pendulum_minimum_since_peak)
        pendulum_minimum_since_peak = next_signal;
    bool local_peak = pendulum_current_signal > pendulum_previous_signal &&
                      pendulum_current_signal >= next_signal;
    if (local_peak && pendulum_current_signal - pendulum_minimum_since_peak >=
                          PENDULUM_PEAK_EXCESS_COUNTS) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (pendulum_last_peak_ms) {
            uint32_t period_ms = now_ms - pendulum_last_peak_ms;
            if (period_ms >= PENDULUM_MIN_PERIOD_MS && period_ms <= PENDULUM_MAX_PERIOD_MS) {
                pendulum_period_total_ms += period_ms;
                ++pendulum_period_count;
                if (pendulum_period_count >= PENDULUM_PERIODS_TO_AVERAGE)
                    pendulum_state = PENDULUM_RESULT;
            }
        }
        pendulum_last_peak_ms = now_ms;
        pendulum_minimum_since_peak = pendulum_current_signal;
    }
    pendulum_previous_signal = pendulum_current_signal;
    pendulum_current_signal = next_signal;
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
        snprintf(line, sizeof line, "PEAK %u/4", (unsigned)pendulum_period_count);
        ssd1306_ui_text(2, 47, line, true);
        ssd1306_ui_text(2, 62, "MID STOP", true);
    } else {
        float period_seconds = (float)pendulum_period_total_ms /
                               ((float)pendulum_period_count * 1000.0f);
        float length_cm = 980.665f * (period_seconds / 6.2831853f) *
                          (period_seconds / 6.2831853f);
        uint32_t period_hundredths = (uint32_t)(period_seconds * 100.0f + 0.5f);
        uint32_t length_tenths = (uint32_t)(length_cm * 10.0f + 0.5f);
        snprintf(line, sizeof line, "PER %lu.%02lu",
                 (unsigned long)(period_hundredths / 100u),
                 (unsigned long)(period_hundredths % 100u));
        ssd1306_ui_text(2, 32, line, true);
        snprintf(line, sizeof line, "LEN %lu.%lu",
                 (unsigned long)(length_tenths / 10u),
                 (unsigned long)(length_tenths % 10u));
        ssd1306_ui_text(2, 47, line, true);
        ssd1306_ui_text(2, 62, "MID AGAIN", true);
        ssd1306_ui_text(2, 112, "DN EXIT", true);
    }
    ssd1306_show();
}
static void pendulum_button(unsigned button) {
    if (button == 1) {
        if (pendulum_state == PENDULUM_READY || pendulum_state == PENDULUM_RESULT) pendulum_arm();
        else pendulum_state = PENDULUM_READY;
    } else if (button == 2 && pendulum_state == PENDULUM_RESULT) {
        app_return_home();
    } else if (button == 0 && pendulum_state != PENDULUM_MEASURING) {
        app_return_home();
    }
    return;
}

static void pendulum_enter(void) { pendulum_state=PENDULUM_READY; }
static void pendulum_sample(const AppSample *sample) { (void)sample; pendulum_process_sample(); show_pendulum(); }
static uint32_t pendulum_period(void) { return pendulum_state!=PENDULUM_READY ? 20 : 250; }
const App pendulum_app = {.enter=pendulum_enter,.draw=show_pendulum,.button=pendulum_button,
    .sample=pendulum_sample,.sample_period_ms=pendulum_period};
