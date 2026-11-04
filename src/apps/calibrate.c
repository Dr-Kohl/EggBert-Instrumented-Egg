#include <stdio.h>
#include <string.h>
#include <math.h>
#include "app.h"
#include "ui.h"
#include "calibration.h"
#include "pico/stdlib.h"

#define CALIBRATION_SAMPLE_COUNT 20u
#define CALIBRATION_SETTLE_MS 2500u
#define CALIBRATION_GRAVITY_MIN  (LIVE_ACCEL_COUNTS_PER_G * 85u / 100u)
#define CALIBRATION_GRAVITY_MAX  (LIVE_ACCEL_COUNTS_PER_G * 115u / 100u)
#define CALIBRATION_DOMINANT_MIN (LIVE_ACCEL_COUNTS_PER_G * 85u / 100u)
#define CALIBRATION_OTHER_MAX    (LIVE_ACCEL_COUNTS_PER_G * 45u / 100u)
#define CALIBRATION_STILL_RANGE  (LIVE_ACCEL_COUNTS_PER_G * 8u / 100u)
static int16_t calibration_positive[3];
static int16_t calibration_negative[3];
static uint8_t calibration_faces_seen;
static uint8_t calibration_samples;
static int64_t calibration_sum[3];
static int16_t calibration_minimum[3];
static int16_t calibration_maximum[3];
static bool calibration_collecting;
static bool calibration_armed;
static absolute_time_t calibration_capture_at;
static bool calibration_complete;
static bool calibration_ready_to_save;
static calibration_record_t calibration_pending;
static char calibration_feedback[12];

static void calibration_reset_wizard(void) {
    calibration_faces_seen = 0;
    calibration_collecting = false;
    calibration_armed = false;
    calibration_complete = false;
    calibration_ready_to_save = false;
    memset(&calibration_pending, 0, sizeof calibration_pending);
    calibration_feedback[0] = '\0';
}

static void calibration_begin_face(void) {
    calibration_collecting = false;
    calibration_armed = true;
    calibration_capture_at = make_timeout_time_ms(CALIBRATION_SETTLE_MS);
    calibration_feedback[0] = '\0';
}

static void calibration_start_collection(void) {
    calibration_samples = 0;
    calibration_collecting = true;
    for (unsigned axis = 0; axis < 3u; ++axis) {
        calibration_sum[axis] = 0;
        calibration_minimum[axis] = INT16_MAX;
        calibration_maximum[axis] = INT16_MIN;
    }
}

static void show_calibration(void) {
    if (!ui_display_available()) return;
    ui_begin("CALIBRATE");
    char progress[10];
    snprintf(progress, sizeof progress, "%u OF 6",
             (unsigned)__builtin_popcount(calibration_faces_seen));
    ssd1306_ui_text(2, 30, progress, true);
    if (calibration_complete) {
        ssd1306_ui_text(2, 43, "SAVED", true);
        ssd1306_ui_text(2, 112, "MID EXIT", true);
    } else if (calibration_ready_to_save) {
        ssd1306_ui_text(2, 45, calibration_feedback[0] ? "SAVE ERR" : "READY", true);
        ssd1306_ui_text(2, 112, "MID SAVE", true);
    } else if (calibration_armed) {
        ssd1306_ui_text(2, 45, "SET DOWN", true);
        ssd1306_ui_text(2, 59, "AUTO CAP", true);
    } else if (calibration_collecting) {
        ssd1306_ui_text(2, 45, "HOLD", true);
        ssd1306_ui_text(2, 59, "STILL", true);
    } else if (calibration_feedback[0]) {
        ssd1306_ui_text(2, 45, calibration_feedback, true);
        ssd1306_ui_text(2, 59, "NEW FACE", true);
        ssd1306_ui_text(2, 112, "MID CAP", true);
    } else {
        ssd1306_ui_text(2, 45, "PLACE", true);
        ssd1306_ui_text(2, 59, "ANY FACE", true);
        ssd1306_ui_text(2, 112, "MID CAP", true);
    }
    ssd1306_ui_text(2, 99, "UP EXIT", true);
    ssd1306_show();
}

static void calibration_finish_face(void) {
    int16_t average[3];
    for (unsigned axis = 0; axis < 3u; ++axis)
        average[axis] = (int16_t)(calibration_sum[axis] / CALIBRATION_SAMPLE_COUNT);
    calibration_collecting = false;

    int64_t magnitude = (int64_t)average[0] * average[0] +
                        (int64_t)average[1] * average[1] +
                        (int64_t)average[2] * average[2];
    if (magnitude < (int64_t)CALIBRATION_GRAVITY_MIN * CALIBRATION_GRAVITY_MIN ||
        magnitude > (int64_t)CALIBRATION_GRAVITY_MAX * CALIBRATION_GRAVITY_MAX) {
        strcpy(calibration_feedback, "NOT 1G");
        return;
    }
    unsigned axis = 0;
    uint32_t absolute[3];
    for (unsigned i = 0; i < 3u; ++i) {
        absolute[i] = (uint32_t)(average[i] < 0 ? -average[i] : average[i]);
        if (absolute[i] > absolute[axis]) axis = i;
        if ((int32_t)calibration_maximum[i] - calibration_minimum[i] > (int32_t)CALIBRATION_STILL_RANGE) {
            strcpy(calibration_feedback, "TOO MOVED");
            return;
        }
    }
    if (absolute[axis] < CALIBRATION_DOMINANT_MIN ||
        absolute[(axis + 1u) % 3u] > CALIBRATION_OTHER_MAX ||
        absolute[(axis + 2u) % 3u] > CALIBRATION_OTHER_MAX) {
        strcpy(calibration_feedback, "TILT MORE");
        return;
    }
    unsigned face = axis * 2u + (average[axis] < 0 ? 1u : 0u);
    if (calibration_faces_seen & (1u << face)) {
        strcpy(calibration_feedback, "DUP FACE");
        return;
    }
    if (average[axis] < 0) calibration_negative[axis] = average[axis];
    else calibration_positive[axis] = average[axis];
    calibration_faces_seen |= 1u << face;
    snprintf(calibration_feedback, sizeof calibration_feedback, "OK %c%c",
             (char)('X' + axis), average[axis] < 0 ? '-' : '+');

    if (calibration_faces_seen != 0x3fu) return;
    calibration_record_t candidate = *calibration_current();
    for (unsigned i = 0; i < 3u; ++i) {
        int32_t span = (int32_t)calibration_positive[i] - calibration_negative[i];
        if (span < (int32_t)(LIVE_ACCEL_COUNTS_PER_G * 16u / 10u)) {
            strcpy(calibration_feedback, "BAD RANGE");
            calibration_faces_seen = 0;
            return;
        }
        candidate.offset_live_counts[i] = ((int32_t)calibration_positive[i] + calibration_negative[i]) / 2;
        candidate.scale_q16[i] = (uint32_t)(((uint64_t)2u * LIVE_ACCEL_COUNTS_PER_G << 16) / span);
    }
    calibration_pending = candidate;
    calibration_ready_to_save = true;
    calibration_feedback[0] = '\0';
}

static void calibration_commit_pending(void) {
    if (!calibration_ready_to_save || calibration_complete) return;
    if (calibration_save(&calibration_pending)) {
        calibration_complete = true;
        calibration_ready_to_save = false;
    } else strcpy(calibration_feedback, "SAVE FAIL");
}

static void calibration_exit(void) {
    bool saved = calibration_complete;
    calibration_reset_wizard();
    app_return_home();
    printf(saved ? "Calibration closed.\n" : "Calibration cancelled; previous calibration retained.\n");
}

static void calibration_process_raw(int16_t x, int16_t y, int16_t z) {
    if (calibration_ready_to_save || calibration_complete) return;
    if (calibration_armed) {
        if (!time_reached(calibration_capture_at)) return;
        calibration_armed = false;
        calibration_start_collection();
    }
    if (!calibration_collecting) return;
    const int16_t values[3] = {x, y, z};
    for (unsigned axis = 0; axis < 3u; ++axis) {
        calibration_sum[axis] += values[axis];
        if (values[axis] < calibration_minimum[axis]) calibration_minimum[axis] = values[axis];
        if (values[axis] > calibration_maximum[axis]) calibration_maximum[axis] = values[axis];
    }
    if (++calibration_samples >= CALIBRATION_SAMPLE_COUNT) calibration_finish_face();
}

static void calibrate_button(unsigned button) {
    if (button==0 || (button==1 && calibration_complete)) calibration_exit();
    else if (button==1) {
        if (calibration_ready_to_save) calibration_commit_pending(); else calibration_begin_face();
    }
}
static void calibrate_sample(const AppSample *sample) {
    calibration_process_raw(sample->raw[0],sample->raw[1],sample->raw[2]); show_calibration();
}
static uint32_t calibrate_period(void) { return calibration_collecting || calibration_armed ? 50 : 250; }
const App calibrate_app = {.enter=calibration_reset_wizard,.leave=calibration_reset_wizard,
    .draw=show_calibration,.button=calibrate_button,.sample=calibrate_sample,.sample_period_ms=calibrate_period};
