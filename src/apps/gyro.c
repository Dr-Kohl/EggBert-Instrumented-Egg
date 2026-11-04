#include <stdio.h>
#include <string.h>
#include <math.h>
#include "app.h"
#include "ui.h"
#include "calibration.h"
#include "pico/stdlib.h"
#include "gyro_session.h"
#include "lsm6dsv.h"

static gyro_session_t gyro_session;
static bool gyro_peak_view = true;
static bool gyro_ok;
static absolute_time_t next_gyro_display;

static void show_gyro(void) {
    if (!ui_display_available()) return;
    ui_begin("GYRO");
    ssd1306_ui_text(2, 30, gyro_peak_view ? "PEAK" : "LIVE", true);
    ssd1306_ui_text(2, 42, "DEG/S", true);
    if (!gyro_ok) {
        ssd1306_ui_text(2, 54, "IMU ERR", true);
    } else if (gyro_session.phase == GYRO_SETTLING || gyro_session.phase == GYRO_ZEROING) {
        ssd1306_ui_text(2, 54, "HOLD", true);
        ssd1306_ui_text(2, 68, "STILL", true);
        ssd1306_ui_text(2, 82, "ZEROING", true);
    } else {
        const int32_t *values = gyro_peak_view ? gyro_session.peak_mdps : gyro_session.live_mdps;
        for (unsigned axis = 0; axis < 3; ++axis) {
            int32_t mdps = values[axis];
            unsigned speed = (unsigned)((gyro_abs(mdps) + 500) / 1000);
            char text[12];
            snprintf(text, sizeof text, "%c %c%u", "XYZ"[axis], mdps < 0 ? '-' : '+', speed);
            ssd1306_ui_text(2, (uint8_t)(54 + axis * 14), text, true);
        }
        bool limit = gyro_peak_view ? gyro_session.peak_limit : gyro_session.live_limit;
        if (gyro_session.phase == GYRO_RESET_WAIT) ssd1306_ui_text(2, 91, "WAIT", true);
        else if (limit) ssd1306_ui_text(2, 91, "LIMIT", true);
    }
    ssd1306_ui_text(2, 100, "UP VIEW", true);
    ssd1306_ui_text(2, 109, "MID RST", true);
    ssd1306_ui_text(2, 118, "DN EXIT", true);
    ssd1306_show();
}

static void gyro_begin(void) {
    gyro_peak_view = true;
    gyro_session_begin(&gyro_session, to_ms_since_boot(get_absolute_time()));
    gyro_ok = lsm6dsv_gyro_start();
    next_gyro_display = get_absolute_time();
    printf("Gyro: %s (240 Hz, +/-2000 deg/s). Hold still for zeroing.\n", gyro_ok ? "OK" : "ERROR");
}

static void service_gyro(void) {
    if (gyro_ok) {
        int16_t raw[3];
        if (lsm6dsv_read_gyro(&raw[0], &raw[1], &raw[2]))
            gyro_session_sample(&gyro_session, raw, to_ms_since_boot(get_absolute_time()));
    }
    if (time_reached(next_gyro_display)) {
        show_gyro();
        next_gyro_display = make_timeout_time_ms(150);
    }
}

static void gyro_leave(void) { lsm6dsv_gyro_stop(); }
static void gyro_button(unsigned button) {
    if (button==0) gyro_peak_view=!gyro_peak_view;
    else if (button==1 && gyro_ok) gyro_session_reset(&gyro_session,to_ms_since_boot(get_absolute_time()));
    else if (button==2) app_return_home();
}
const App gyro_app = {.enter=gyro_begin,.leave=gyro_leave,.draw=show_gyro,
    .button=gyro_button,.tick=service_gyro,.fast_tick=true};
