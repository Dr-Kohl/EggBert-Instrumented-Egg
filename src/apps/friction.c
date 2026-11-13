#include <stdio.h>
#include <string.h>
#include <math.h>
#include "app.h"
#include "ui.h"
#include "calibration.h"
#include "pico/stdlib.h"

#define FRICTION_MAX_TRIALS        5u
#define FRICTION_MOTION_COUNTS     (LIVE_ACCEL_COUNTS_PER_G * 6u / 100u)
#define FRICTION_REFERENCE_ALPHA   0.12f
#define FRICTION_SETTLE_SAMPLES    8u
#define FRICTION_ARM_DELAY_MS   2500u
#define LEVEL_DISPLAY_PERIOD_MS 200u
typedef enum {
    FRICTION_READY,
    FRICTION_ARMED,
    FRICTION_RESULT,
    FRICTION_SUMMARY,
} friction_state_t;

static friction_state_t friction_state;
static float friction_reference_x;
static float friction_reference_y;
static float friction_reference_z;
static float friction_last_stable_angle;
static float friction_trial_angles[FRICTION_MAX_TRIALS];
static float friction_trial_mu[FRICTION_MAX_TRIALS];
static uint8_t friction_trial_count;
static uint8_t friction_settle_count;
static uint8_t friction_motion_count;
static uint8_t friction_summary_selection;
static bool friction_reference_valid;
static bool friction_waiting_to_arm;
static absolute_time_t friction_arm_at;

static absolute_time_t next_friction_display;

static float friction_angle_from_vector(float x, float y, float z) {
    const float radians_to_degrees = 57.2957795f;
    // EggBert sits on its broad Z face.  The horizontal X/Y gravity component
    // is the down-ramp component, independent of which way the ramp is aimed.
    return atan2f(sqrtf(x * x + y * y), fabsf(z)) * radians_to_degrees;
}

static void format_friction_value(char *text, size_t text_size,
                                  const char *label, float value) {
    uint32_t hundredths = (uint32_t)(value * 100.0f + 0.5f);
    snprintf(text, text_size, "%s%lu.%02lu", label,
             (unsigned long)(hundredths / 100u),
             (unsigned long)(hundredths % 100u));
}

static void format_friction_mu(char *text, size_t text_size, float value) {
    uint32_t hundredths = (uint32_t)(value * 100.0f + 0.5f);
    snprintf(text, text_size, "%lu.%02lu", (unsigned long)(hundredths / 100u),
             (unsigned long)(hundredths % 100u));
}

static float friction_average_mu(void) {
    float total = 0.0f;
    for (unsigned i = 0; i < friction_trial_count; ++i) total += friction_trial_mu[i];
    return friction_trial_count ? total / friction_trial_count : 0.0f;
}

static void friction_reset_session(void) {
    friction_state = FRICTION_READY;
    friction_trial_count = 0;
    friction_reference_valid = false;
    friction_waiting_to_arm = false;
    friction_settle_count = 0;
    friction_motion_count = 0;
    friction_summary_selection = 0;
}

static void friction_arm(void) {
    friction_state = FRICTION_ARMED;
    friction_reference_valid = false;
    friction_settle_count = 0;
    friction_motion_count = 0;
    friction_waiting_to_arm = true;
    friction_arm_at = make_timeout_time_ms(FRICTION_ARM_DELAY_MS);
}

static void friction_process_sample(void) {
    if (friction_state != FRICTION_ARMED) return;
    if (friction_waiting_to_arm) {
        if (!time_reached(friction_arm_at)) return;
        friction_waiting_to_arm = false;
        friction_reference_valid = false;
        friction_settle_count = 0;
    }
    float x = (float)app_sample_current()->corrected[0];
    float y = (float)app_sample_current()->corrected[1];
    float z = (float)app_sample_current()->corrected[2];
    if (!friction_reference_valid) {
        friction_reference_x = x;
        friction_reference_y = y;
        friction_reference_z = z;
        friction_last_stable_angle = friction_angle_from_vector(x, y, z);
        friction_reference_valid = true;
        friction_settle_count = 1;
        return;
    }
    float dx = x - friction_reference_x;
    float dy = y - friction_reference_y;
    float dz = z - friction_reference_z;
    float motion = sqrtf(dx * dx + dy * dy + dz * dz);
    if (motion > FRICTION_MOTION_COUNTS) {
        if (friction_motion_count < UINT8_MAX) ++friction_motion_count;
    } else {
        friction_motion_count = 0;
    }
    if (friction_settle_count >= FRICTION_SETTLE_SAMPLES && friction_motion_count >= 2u) {
        float angle_radians = friction_last_stable_angle / 57.2957795f;
        friction_trial_angles[friction_trial_count] = friction_last_stable_angle;
        friction_trial_mu[friction_trial_count] = tanf(angle_radians);
        ++friction_trial_count;
        if (friction_trial_count == FRICTION_MAX_TRIALS) {
            friction_state = FRICTION_SUMMARY;
            friction_summary_selection = 0;
        } else {
            friction_state = FRICTION_RESULT;
        }
        printf("Friction slide: angle %.2f deg, mu_s %.3f (trial %u)\n",
               (double)friction_last_stable_angle,
               (double)friction_trial_mu[friction_trial_count - 1u],
               (unsigned)friction_trial_count);
        return;
    }
    friction_reference_x += FRICTION_REFERENCE_ALPHA * dx;
    friction_reference_y += FRICTION_REFERENCE_ALPHA * dy;
    friction_reference_z += FRICTION_REFERENCE_ALPHA * dz;
    friction_last_stable_angle = friction_angle_from_vector(
        friction_reference_x, friction_reference_y, friction_reference_z);
    if (friction_settle_count < FRICTION_SETTLE_SAMPLES) ++friction_settle_count;
}

static void friction_delete_trial(unsigned trial) {
    if (trial >= friction_trial_count) return;
    for (unsigned i = trial; i + 1u < friction_trial_count; ++i) {
        friction_trial_angles[i] = friction_trial_angles[i + 1u];
        friction_trial_mu[i] = friction_trial_mu[i + 1u];
    }
    --friction_trial_count;
    if (friction_summary_selection >= friction_trial_count && friction_trial_count)
        friction_summary_selection = friction_trial_count - 1u;
}

static void show_friction(void) {
    if (!ui_display_available()) return;
    char line[14];
    char mu[8];
    ui_begin(friction_state==FRICTION_SUMMARY ? "TRIALS" : "FRICTION");
    if (friction_state == FRICTION_READY) {
        ssd1306_ui_text(2, 32, "SET LOW", true);
        ssd1306_ui_text(2, 46, "MID ARM", true);
        ssd1306_ui_text(2, 112, "UP HOME", true);
    } else if (friction_state == FRICTION_ARMED) {
        if (friction_waiting_to_arm) {
            ssd1306_ui_text(2, 32, "SET DOWN", true);
            ssd1306_ui_text(2, 47, "AUTO ARM", true);
        } else {
            ssd1306_ui_text(2, 32, "TILT SLOW", true);
            format_friction_value(line, sizeof line, "ANG ", friction_last_stable_angle);
            ssd1306_ui_text(2, 47, line, true);
            ssd1306_ui_text(2, 62, "MID STOP", true);
        }
    } else if (friction_state == FRICTION_RESULT) {
        snprintf(line, sizeof line, "TRIAL %u", (unsigned)friction_trial_count);
        ssd1306_ui_text(2, 30, line, true);
        format_friction_value(line, sizeof line, "ANG ", friction_trial_angles[friction_trial_count - 1u]);
        ssd1306_ui_text(2, 40, line, true);
        format_friction_mu(mu, sizeof mu, friction_trial_mu[friction_trial_count - 1u]);
        snprintf(line, sizeof line, "MU %s", mu);
        ssd1306_ui_text(2, 54, line, true);
        format_friction_mu(mu, sizeof mu, friction_average_mu());
        snprintf(line, sizeof line, "AVG %s", mu);
        ssd1306_ui_text(2, 68, line, true);
        ssd1306_ui_text(2, 88, "UP HOME", true);
        ssd1306_ui_text(2, 100, "MID AGAIN", true);
        ssd1306_ui_text(2, 112, "DN LIST", true);
    } else {
        format_friction_mu(mu, sizeof mu, friction_average_mu());
        snprintf(line, sizeof line, "AVG %s", mu);
        ssd1306_ui_text(2, 30, line, true);
        for (unsigned i = 0; i < friction_trial_count; ++i) {
            uint8_t y = (uint8_t)(44u + 11u * i);
            bool selected = friction_summary_selection == i;
            if (selected) ssd1306_ui_fill_rect(0, (uint8_t)(y - 1u), 64, 10, true);
            format_friction_mu(mu, sizeof mu, friction_trial_mu[i]);
            snprintf(line, sizeof line, "T%u %s", i + 1u, mu);
            ssd1306_ui_text(2, y, line, !selected);
        }
        bool rearm_selected = friction_summary_selection == friction_trial_count;
        bool exit_selected = friction_summary_selection == friction_trial_count + 1u;
        if (rearm_selected) ssd1306_ui_fill_rect(0, 103, 64, 10, true);
        ssd1306_ui_text(2, 104, "REARM", !rearm_selected);
        if (exit_selected) ssd1306_ui_fill_rect(0, 115, 64, 10, true);
        ssd1306_ui_text(2, 116, "EXIT", !exit_selected);
    }
    ssd1306_show();
}
static void friction_button(unsigned button) {
    if (friction_state == FRICTION_SUMMARY) {
        unsigned choices = friction_trial_count + 2u; // trials, REARM, EXIT
        if (button == 0) {
            friction_summary_selection = (friction_summary_selection + choices - 1u) % choices;
        } else if (button == 2) {
            friction_summary_selection = (friction_summary_selection + 1u) % choices;
        } else if (button == 1) {
            if (friction_summary_selection < friction_trial_count) {
                friction_delete_trial(friction_summary_selection);
            } else if (friction_summary_selection == friction_trial_count) {
                friction_arm();
            } else {
                app_return_home();
            }
        }
        return;
    }
    if (button == 1) {
        if (friction_state == FRICTION_READY) friction_arm();
        else if (friction_state == FRICTION_ARMED) friction_state = FRICTION_READY;
        else if (friction_state == FRICTION_RESULT) friction_arm();
    } else if (button == 2) {
        if (friction_state == FRICTION_RESULT) {
            friction_state = FRICTION_SUMMARY;
            friction_summary_selection = 0;
        }
    } else if (button == 0 && friction_state != FRICTION_ARMED) {
        app_return_home();
    }
    return;
}

static void friction_sample(const AppSample *sample) {
    (void)sample; friction_process_sample();
    if (time_reached(next_friction_display)) { show_friction(); next_friction_display=make_timeout_time_ms(LEVEL_DISPLAY_PERIOD_MS); }
}
static uint32_t friction_period(void) { return friction_state==FRICTION_ARMED ? 20 : 250; }
static bool friction_can_idle(void) { return friction_state != FRICTION_ARMED; }
const App friction_app = {.can_idle=friction_can_idle,.enter=friction_reset_session,.draw=show_friction,.button=friction_button,
    .sample=friction_sample,.sample_period_ms=friction_period};
