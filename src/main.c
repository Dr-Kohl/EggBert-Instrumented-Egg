#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "board.h"
#include "hardware/adc.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "lsm6dsv.h"
#include "gyro_session.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "ssd1306.h"

static const uint LEDS[] = {LED_GREEN_PIN, LED_YELLOW_PIN, LED_RED_PIN};
// Physical top, middle, and bottom buttons.  The schematic switch numbering
// runs bottom-to-top, so it is deliberately not the navigation order.
static const uint BUTTONS[] = {BUTTON_3_PIN, BUTTON_2_PIN, BUTTON_1_PIN};

#define CAPTURE_RATE_HZ       3840u
#define CAPTURE_SECONDS         5u
#define CAPTURE_MAX_SAMPLES   (CAPTURE_RATE_HZ * CAPTURE_SECONDS)
#define PRE_TRIGGER_SAMPLES   (CAPTURE_RATE_HZ / 2u)
// The trigger sample itself lies between these two windows.
#define POST_TRIGGER_SAMPLES  (CAPTURE_MAX_SAMPLES - PRE_TRIGGER_SAMPLES - 1u)

// At +/-16 g, the LSM6DSV sensitivity is about 0.488 mg/count.
#define ACCEL_COUNTS_PER_G     2048u
#define LIVE_ACCEL_COUNTS_PER_G 16384u
#define STILL_MIN_COUNTS       (ACCEL_COUNTS_PER_G * 8u / 10u)
#define STILL_MAX_COUNTS       (ACCEL_COUNTS_PER_G * 12u / 10u)
#define FREEFALL_MAX_COUNTS    (ACCEL_COUNTS_PER_G * 45u / 100u)
#define STILL_REQUIRED_SAMPLES CAPTURE_RATE_HZ
#define GENTLE_CATCH_START_COUNTS (ACCEL_COUNTS_PER_G * 8u / 10u)
#define GENTLE_CATCH_SAMPLES      (CAPTURE_RATE_HZ * 15u / 100u)
#define GENTLE_REARM_STILL_SAMPLES (CAPTURE_RATE_HZ / 2u)
#define GENTLE_FREEFALL_SAMPLES   (CAPTURE_RATE_HZ / 10u)
#define GENTLE_FIRM_MIN_COUNTS    (ACCEL_COUNTS_PER_G * 5u)
#define GENTLE_HARD_MIN_COUNTS    (ACCEL_COUNTS_PER_G * 10u)
#define GENTLE_RESULT_LED_MS      5000u
#define SATURATION_NEAR_COUNTS    (ACCEL_COUNTS_PER_G * 159u / 10u)

#define EGG_FILE_VERSION        1u
#define EGG_FILE_HEADER_BYTES   32u
#define EGG_FILE_SAMPLE_BYTES    6u
#define EGG_FLAG_TRIGGERED      0x01u
#define EGG_FLAG_FREEFALL       0x02u
#define EGG_FLAG_FIFO_OVERRUN   0x04u

#define BATTERY_ADC_INPUT        3u
#define BATTERY_ADC_SAMPLES      16u
#define USB_PRESENT_SET_MV     4400u
#define USB_PRESENT_CLEAR_MV   4300u

#define CALIBRATION_MAGIC       0x43414C31u /* CAL1 */
#define CALIBRATION_VERSION      1u
#define CALIBRATION_FLASH_BYTES  (2u * FLASH_SECTOR_SIZE)
#define CALIBRATION_FLASH_A      (PICO_FLASH_SIZE_BYTES - CALIBRATION_FLASH_BYTES)
#define CALIBRATION_FLASH_B      (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define CALIBRATION_SAMPLE_COUNT 20u
#define CALIBRATION_SETTLE_MS 2500u
#define CALIBRATION_GRAVITY_MIN  (LIVE_ACCEL_COUNTS_PER_G * 85u / 100u)
#define CALIBRATION_GRAVITY_MAX  (LIVE_ACCEL_COUNTS_PER_G * 115u / 100u)
#define CALIBRATION_DOMINANT_MIN (LIVE_ACCEL_COUNTS_PER_G * 85u / 100u)
#define CALIBRATION_OTHER_MAX    (LIVE_ACCEL_COUNTS_PER_G * 45u / 100u)
#define CALIBRATION_STILL_RANGE  (LIVE_ACCEL_COUNTS_PER_G * 8u / 100u)
#define LEVEL_FILTER_TIME_MS     200u
#define LEVEL_DISPLAY_PERIOD_MS  200u
#define FRICTION_MAX_TRIALS        5u
#define FRICTION_MOTION_COUNTS     (LIVE_ACCEL_COUNTS_PER_G * 6u / 100u)
#define FRICTION_REFERENCE_ALPHA   0.12f
#define FRICTION_SETTLE_SAMPLES    8u
#define FRICTION_ARM_DELAY_MS   2500u
#define PENDULUM_ARM_DELAY_MS   2500u
#define PENDULUM_PEAK_EXCESS_COUNTS (LIVE_ACCEL_COUNTS_PER_G * 5u / 1000u)
#define PENDULUM_MIN_PERIOD_MS   300u
#define PENDULUM_MAX_PERIOD_MS  8000u
#define PENDULUM_PERIODS_TO_AVERAGE 4u

// 19,200 samples x 3 axes x 16 bits = 115,200 bytes.  This is intentionally
// RAM-only for the first capture bring-up; no flash writes occur.
static lsm6dsv_accel_sample_t capture_samples[CAPTURE_MAX_SAMPLES];
static lsm6dsv_accel_sample_t fifo_samples[64];
static size_t capture_count;
static size_t capture_write_index;
static size_t capture_oldest_index;
static size_t trigger_index;
static uint32_t still_samples;
static uint32_t post_trigger_samples;
static bool capture_active;
static bool capture_complete;
static bool capture_fifo_overrun;
static bool trigger_was_freefall;
static bool trigger_detected;
static absolute_time_t next_capture_poll;
static bool oled_ok;
static uint16_t power_out_millivolts;
static bool usb_power_present;
static int16_t live_accel_x;
static int16_t live_accel_y;
static int16_t live_accel_z;
static bool orientation_stream;
static bool orientation_fused;
static uint32_t orientation_last_ms;
static float level_accel_x;
static float level_accel_y;
static float level_accel_z;
static uint32_t level_filter_last_ms;
static bool level_filter_initialized;
static absolute_time_t next_level_display;
static absolute_time_t next_friction_display;

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

typedef enum { BEAM_READY, BEAM_WAITING, BEAM_LISTENING, BEAM_RESULT } beam_state_t;
static beam_state_t beam_state;
static absolute_time_t beam_arm_at;
static float beam_baseline[3], beam_previous, beam_current;
static uint32_t beam_samples, beam_crossings, beam_first_cross, beam_last_cross;
static unsigned beam_axis;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t generation;
    int32_t offset_live_counts[3];
    uint32_t scale_q16[3];
    uint32_t crc32;
} calibration_record_t;

static calibration_record_t calibration;
static bool calibration_valid;
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
static char calibration_feedback[12];

typedef enum {
    UI_HOME,
    UI_RECORD,
    UI_DROP_TEST,
    UI_CHALLENGES,
    UI_ACCELERATION,
    UI_GYRO,
    UI_RAW_XYZ,
    UI_INCLINOMETER,
    UI_CALIBRATION,
    UI_FRICTION,
    UI_PENDULUM,
    UI_BEAM,
    UI_GENTLE_CATCH,
    UI_CAPTURE_COMPLETE,
    UI_CAPTURE_OPTIONS,
    UI_ERASE_CONFIRM,
} ui_state_t;

static ui_state_t ui_state = UI_HOME;
static unsigned ui_selection;
static const unsigned home_menu_order[] = {0, 1, 2, 3, 9, 4, 5, 6, 7, 8};
static gyro_session_t gyro_session;
static bool gyro_peak_view = true;
static bool gyro_ok;
static absolute_time_t next_gyro_display;

typedef enum {
    CAPTURE_IDLE,
    CAPTURE_WAIT_STILL,
    CAPTURE_WAIT_EVENT,
    CAPTURE_POST_EVENT,
    CAPTURE_COMPLETE,
    CAPTURE_GENTLE_WAIT_FALL,
    CAPTURE_GENTLE_WAIT_CATCH,
    CAPTURE_GENTLE_MEASURE,
    CAPTURE_GENTLE_RESULT,
} capture_state_t;

static capture_state_t capture_state = CAPTURE_IDLE;
static uint32_t gentle_measure_samples;
static uint32_t gentle_peak_axis_counts;
static uint32_t gentle_rearm_still_samples;
static uint32_t gentle_freefall_samples;
static bool gentle_saturated;
static bool gentle_result_available;
static absolute_time_t gentle_result_led_until;

typedef enum {
    GENTLE_SCORE_GENTLE,
    GENTLE_SCORE_FIRM,
    GENTLE_SCORE_HARD,
} gentle_score_t;

static uint32_t calibration_crc32(const calibration_record_t *record) {
    uint32_t crc = 0xffffffffu;
    const uint8_t *bytes = (const uint8_t *)record;
    for (size_t i = 0; i < offsetof(calibration_record_t, crc32); ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

static bool calibration_record_valid(const calibration_record_t *record) {
    if (record->magic != CALIBRATION_MAGIC || record->version != CALIBRATION_VERSION ||
        record->crc32 != calibration_crc32(record)) return false;
    for (unsigned axis = 0; axis < 3u; ++axis)
        if (record->scale_q16[axis] < 32768u || record->scale_q16[axis] > 131072u) return false;
    return true;
}

static void calibration_load(void) {
    const calibration_record_t *first = (const calibration_record_t *)(XIP_BASE + CALIBRATION_FLASH_A);
    const calibration_record_t *second = (const calibration_record_t *)(XIP_BASE + CALIBRATION_FLASH_B);
    bool first_valid = calibration_record_valid(first);
    bool second_valid = calibration_record_valid(second);
    calibration_valid = first_valid || second_valid;
    if (!calibration_valid) return;
    calibration = (!first_valid || (second_valid && second->generation > first->generation)) ? *second : *first;
    printf("Loaded accelerometer calibration generation %lu.\n", (unsigned long)calibration.generation);
}

static int16_t calibration_correct_axis(int16_t raw, unsigned axis, uint32_t counts_per_g) {
    if (!calibration_valid) return raw;
    int32_t offset = (int32_t)(((int64_t)calibration.offset_live_counts[axis] * counts_per_g) /
                               LIVE_ACCEL_COUNTS_PER_G);
    int64_t corrected = ((int64_t)(raw - offset) * calibration.scale_q16[axis] + 32768) >> 16;
    if (corrected > INT16_MAX) return INT16_MAX;
    if (corrected < INT16_MIN) return INT16_MIN;
    return (int16_t)corrected;
}

static void calibration_apply_capture_sample(lsm6dsv_accel_sample_t *sample) {
    sample->x = calibration_correct_axis(sample->x, 0u, ACCEL_COUNTS_PER_G);
    sample->y = calibration_correct_axis(sample->y, 1u, ACCEL_COUNTS_PER_G);
    sample->z = calibration_correct_axis(sample->z, 2u, ACCEL_COUNTS_PER_G);
}

static bool calibration_save(void) {
    calibration_record_t record = calibration;
    record.magic = CALIBRATION_MAGIC;
    record.version = CALIBRATION_VERSION;
    record.generation = calibration_valid ? calibration.generation + 1u : 1u;
    record.crc32 = calibration_crc32(&record);
    uint32_t destination = (calibration_valid &&
        calibration.generation % 2u == 1u) ? CALIBRATION_FLASH_B : CALIBRATION_FLASH_A;
    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof page);
    memcpy(page, &record, sizeof record);
    uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(destination, FLASH_SECTOR_SIZE);
    flash_range_program(destination, page, sizeof page);
    restore_interrupts(interrupts);
    const calibration_record_t *saved = (const calibration_record_t *)(XIP_BASE + destination);
    if (!calibration_record_valid(saved)) return false;
    calibration = *saved;
    calibration_valid = true;
    printf("Saved accelerometer calibration generation %lu.\n", (unsigned long)calibration.generation);
    return true;
}

static void setup_gpio(void) {
    for (unsigned i = 0; i < 3; ++i) {
        gpio_init(LEDS[i]); gpio_set_dir(LEDS[i], GPIO_OUT); gpio_put(LEDS[i], false);
        gpio_init(BUTTONS[i]); gpio_set_dir(BUTTONS[i], GPIO_IN); gpio_pull_up(BUTTONS[i]);
    }
    adc_init();
    adc_gpio_init(29u); // ADC3 senses Out+ through the 100 kOhm / 100 kOhm divider.
    adc_select_input(BATTERY_ADC_INPUT);
}

static void leds(unsigned mask) {
    for (unsigned i = 0; i < 3; ++i) gpio_put(LEDS[i], (mask >> i) & 1u);
}

static void ui_draw_power_indicator(void) {
    if (usb_power_present) {
        // Tall plug icon, drawn as yellow pixels on the black status rail.
        ssd1306_ui_fill_rect(4, 7, 9, 10, true);
        ssd1306_ui_fill_rect(6, 2, 2, 5, true);
        ssd1306_ui_fill_rect(10, 2, 2, 5, true);
        ssd1306_ui_fill_rect(7, 17, 3, 12, true);
        return;
    }

    unsigned bars = power_out_millivolts >= 4050u ? 3u :
                    power_out_millivolts >= 3850u ? 2u :
                    power_out_millivolts >= 3650u ? 1u : 0u;
    // Tall battery outline and stacked bars, all yellow on the black rail.
    ssd1306_ui_fill_rect(4, 3, 8, 2, true);
    ssd1306_ui_fill_rect(2, 5, 2, 24, true);
    ssd1306_ui_fill_rect(12, 5, 2, 24, true);
    ssd1306_ui_fill_rect(4, 29, 8, 2, true);
    ssd1306_ui_fill_rect(6, 0, 4, 3, true);
    for (unsigned bar = 0; bar < bars; ++bar)
        ssd1306_ui_fill_rect(5, (uint8_t)(23u - bar * 6u), 6, 3, true);
}

static void ui_begin(const char *title, const char *rail_status) {
    ssd1306_clear();
    // The physical yellow band maps to this black status rail. Only its
    // meaningful pixels are illuminated, so it does not overpower the UI.
    ui_draw_power_indicator();
    ssd1306_ui_text(5, 40, rail_status, true);
    ssd1306_ui_text(18, 4, title, true);
}

static void ui_menu_item(uint8_t y, const char *text, bool selected) {
    if (selected) ssd1306_ui_fill_rect(17, (uint8_t)(y - 1u), 47, 10, true);
    ssd1306_ui_text(17, y, text, !selected);
}

static void show_home(void) {
    if (!oled_ok) return;
    bool second_page = ui_selection >= 6 && ui_selection <= 8;
    ui_begin(second_page ? "HOME 2" : "HOME 1", "I");
    if (second_page) {
        ui_menu_item(25, "BEAM", ui_selection == 6);
        ui_menu_item(38, "RAW XYZ", ui_selection == 7);
        ui_menu_item(51, "CAL", ui_selection == 8);
    }
    else {
    ui_menu_item(25, "RECORD", ui_selection == 0); ui_menu_item(38, "CATCH", ui_selection == 1);
    ui_menu_item(51, "PENDULUM", ui_selection == 2); ui_menu_item(64, "ACCEL (g)", ui_selection == 3);
    ui_menu_item(77, "GYRO", ui_selection == 9); ui_menu_item(90, "LEVEL", ui_selection == 4);
    ui_menu_item(103, "FRICTION", ui_selection == 5); }
    ssd1306_ui_text(18, 116, "MID GO", true);
    ssd1306_show();
}

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
    if (!oled_ok) return;
    char text[13];
    ui_begin("ACCELERATION", "I");
    format_acceleration(text, sizeof text, 'X', live_accel_x);
    ssd1306_ui_text(18, 28, text, true);
    format_acceleration(text, sizeof text, 'Y', live_accel_y);
    ssd1306_ui_text(18, 44, text, true);
    format_acceleration(text, sizeof text, 'Z', live_accel_z);
    ssd1306_ui_text(18, 60, text, true);
    int32_t magnitude = (int32_t)sqrtf((float)live_accel_x * live_accel_x +
                                       (float)live_accel_y * live_accel_y +
                                       (float)live_accel_z * live_accel_z);
    format_acceleration(text, sizeof text, 'M', magnitude);
    ssd1306_ui_text(18, 82, text, true);
    ssd1306_ui_text(18, 112, "MID EXIT", true);
    ssd1306_show();
}

static void show_gyro(void) {
    if (!oled_ok) return;
    ui_begin("GYRO", "I");
    ssd1306_ui_text(18, 24, gyro_peak_view ? "PEAK" : "LIVE", true);
    ssd1306_ui_text(18, 38, "deg/s", true);
    if (!gyro_ok) {
        ssd1306_ui_text(18, 54, "IMU ERR", true);
    } else if (gyro_session.phase == GYRO_SETTLING || gyro_session.phase == GYRO_ZEROING) {
        ssd1306_ui_text(18, 54, "HOLD", true);
        ssd1306_ui_text(18, 68, "STILL", true);
        ssd1306_ui_text(18, 82, "ZEROING", true);
    } else {
        const int32_t *values = gyro_peak_view ? gyro_session.peak_mdps : gyro_session.live_mdps;
        for (unsigned axis = 0; axis < 3; ++axis) {
            int32_t mdps = values[axis];
            unsigned speed = (unsigned)((gyro_abs(mdps) + 500) / 1000);
            char text[12];
            snprintf(text, sizeof text, "%c %c%u", "XYZ"[axis], mdps < 0 ? '-' : '+', speed);
            ssd1306_ui_text(18, (uint8_t)(54 + axis * 14), text, true);
        }
        bool limit = gyro_peak_view ? gyro_session.peak_limit : gyro_session.live_limit;
        if (gyro_session.phase == GYRO_RESET_WAIT) ssd1306_ui_text(18, 91, "WAIT", true);
        else if (limit) ssd1306_ui_text(18, 91, "LIMIT", true);
    }
    ssd1306_ui_text(18, 100, "UP VIEW", true);
    ssd1306_ui_text(18, 109, "MID RST", true);
    ssd1306_ui_text(18, 118, "DN EXIT", true);
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
    if (ui_state != UI_GYRO || capture_active) return;
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

static unsigned home_step(bool down) {
    unsigned count = sizeof home_menu_order / sizeof home_menu_order[0];
    for (unsigned i = 0; i < count; ++i)
        if (home_menu_order[i] == ui_selection)
            return home_menu_order[(i + (down ? 1 : count - 1)) % count];
    return 0;
}

static void format_tilt(char *text, size_t text_size, char axis, float degrees) {
    // Keep formatting integer-only: the compact Pico printf configuration need
    // not enable floating-point conversion just for this display.
    int32_t tenths = (int32_t)(degrees * 10.0f + (degrees >= 0.0f ? 0.5f : -0.5f));
    char sign = tenths < 0 ? '-' : '+';
    uint32_t magnitude = (uint32_t)(tenths < 0 ? -tenths : tenths);
    snprintf(text, text_size, "%c%c%lu.%lu", axis, sign,
             (unsigned long)(magnitude / 10u), (unsigned long)(magnitude % 10u));
}

static void show_raw_xyz(void) {
    if (!oled_ok) return;
    char raw[8];
    ui_begin("RAW XYZ", "I");
    ssd1306_ui_text(18, 28, "X", true);
    snprintf(raw, sizeof raw, "%d", live_accel_x);
    ssd1306_ui_text(28, 28, raw, true);
    ssd1306_ui_text(18, 44, "Y", true);
    snprintf(raw, sizeof raw, "%d", live_accel_y);
    ssd1306_ui_text(28, 44, raw, true);
    ssd1306_ui_text(18, 60, "Z", true);
    snprintf(raw, sizeof raw, "%d", live_accel_z);
    ssd1306_ui_text(28, 60, raw, true);
    ssd1306_ui_text(18, 112, "MID EXIT", true);
    ssd1306_show();
}

static void update_level_filter(void) {
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if (!level_filter_initialized) {
        level_accel_x = (float)live_accel_x;
        level_accel_y = (float)live_accel_y;
        level_accel_z = (float)live_accel_z;
        level_filter_last_ms = now_ms;
        level_filter_initialized = true;
        return;
    }
    uint32_t elapsed_ms = now_ms - level_filter_last_ms;
    level_filter_last_ms = now_ms;
    float alpha = (float)elapsed_ms / (LEVEL_FILTER_TIME_MS + (float)elapsed_ms);
    level_accel_x += alpha * ((float)live_accel_x - level_accel_x);
    level_accel_y += alpha * ((float)live_accel_y - level_accel_y);
    level_accel_z += alpha * ((float)live_accel_z - level_accel_z);
}

static void show_inclinometer(void) {
    if (!oled_ok) return;
    const float radians_to_degrees = 57.2957795f;
    // A positive X (or Y) acceleration produces a positive displayed X (or
    // Y) tilt. The other horizontal axis is included to keep the result
    // accurate even when EggBert is not level in that direction.
    float x_degrees = atan2f(level_accel_x,
                             sqrtf(level_accel_y * level_accel_y +
                                   level_accel_z * level_accel_z)) * radians_to_degrees;
    float y_degrees = atan2f(level_accel_y,
                             sqrtf(level_accel_x * level_accel_x +
                                   level_accel_z * level_accel_z)) * radians_to_degrees;
    char x_text[8], y_text[8];
    format_tilt(x_text, sizeof x_text, 'X', x_degrees);
    format_tilt(y_text, sizeof y_text, 'Y', y_degrees);
    ui_begin("LEVEL", "I");
    ssd1306_ui_text(18, 22, "DEGREES", true);
    ssd1306_ui_text_double_height(18, 36, x_text, true);
    ssd1306_ui_text_double_height(18, 62, y_text, true);
    ssd1306_ui_text(18, 112, "MID EXIT", true);
    ssd1306_show();
}

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
    float x = (float)live_accel_x;
    float y = (float)live_accel_y;
    float z = (float)live_accel_z;
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
    if (!oled_ok) return;
    char line[14];
    char mu[8];
    ui_begin("", "I");
    if (friction_state == FRICTION_READY) {
        ssd1306_ui_text(16, 32, "SET LOW", true);
        ssd1306_ui_text(16, 46, "MID ARM", true);
    } else if (friction_state == FRICTION_ARMED) {
        if (friction_waiting_to_arm) {
            ssd1306_ui_text(16, 32, "SET DOWN", true);
            ssd1306_ui_text(16, 47, "AUTO ARM", true);
        } else {
            ssd1306_ui_text(16, 32, "TILT SLOW", true);
            format_friction_value(line, sizeof line, "ANG ", friction_last_stable_angle);
            ssd1306_ui_text(16, 47, line, true);
            ssd1306_ui_text(16, 62, "MID STOP", true);
        }
    } else if (friction_state == FRICTION_RESULT) {
        snprintf(line, sizeof line, "TRIAL %u", (unsigned)friction_trial_count);
        ssd1306_ui_text(16, 26, line, true);
        format_friction_value(line, sizeof line, "ANG ", friction_trial_angles[friction_trial_count - 1u]);
        ssd1306_ui_text(16, 40, line, true);
        format_friction_mu(mu, sizeof mu, friction_trial_mu[friction_trial_count - 1u]);
        snprintf(line, sizeof line, "MU %s", mu);
        ssd1306_ui_text(16, 54, line, true);
        format_friction_mu(mu, sizeof mu, friction_average_mu());
        snprintf(line, sizeof line, "AVG %s", mu);
        ssd1306_ui_text(16, 68, line, true);
        ssd1306_ui_text(16, 112, "MID AGAIN", true);
    } else {
        format_friction_mu(mu, sizeof mu, friction_average_mu());
        snprintf(line, sizeof line, "AVG %s", mu);
        ssd1306_ui_text(16, 4, line, true);
        for (unsigned i = 0; i < friction_trial_count; ++i) {
            uint8_t y = (uint8_t)(20u + 12u * i);
            bool selected = friction_summary_selection == i;
            if (selected) ssd1306_ui_fill_rect(17, (uint8_t)(y - 1u), 96, 10, true);
            format_friction_mu(mu, sizeof mu, friction_trial_mu[i]);
            snprintf(line, sizeof line, "T%u %s", i + 1u, mu);
            ssd1306_ui_text(16, y, line, !selected);
        }
        bool rearm_selected = friction_summary_selection == friction_trial_count;
        bool exit_selected = friction_summary_selection == friction_trial_count + 1u;
        if (rearm_selected) ssd1306_ui_fill_rect(17, 91, 47, 10, true);
        ssd1306_ui_text(16, 92, "REARM", !rearm_selected);
        if (exit_selected) ssd1306_ui_fill_rect(17, 103, 47, 10, true);
        ssd1306_ui_text(16, 104, "EXIT", !exit_selected);
    }
    ssd1306_show();
}

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
    float raw_signal = fabsf((float)live_accel_x);
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
    if (!oled_ok) return;
    char line[12];
    ui_begin("PEND", "I");
    if (pendulum_state == PENDULUM_READY) {
        ssd1306_ui_text(16, 32, "HOLD UP", true);
        ssd1306_ui_text(16, 46, "MID ARM", true);
    } else if (pendulum_state == PENDULUM_WAITING) {
        ssd1306_ui_text(16, 32, "HOLD", true);
        ssd1306_ui_text(16, 47, "AUTO ARM", true);
    } else if (pendulum_state == PENDULUM_MEASURING) {
        ssd1306_ui_text(16, 32, "RELEASE", true);
        snprintf(line, sizeof line, "PEAK %u/4", (unsigned)pendulum_period_count);
        ssd1306_ui_text(16, 47, line, true);
        ssd1306_ui_text(16, 62, "MID STOP", true);
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
        ssd1306_ui_text(16, 32, line, true);
        snprintf(line, sizeof line, "LEN %lu.%lu",
                 (unsigned long)(length_tenths / 10u),
                 (unsigned long)(length_tenths % 10u));
        ssd1306_ui_text(16, 47, line, true);
        ssd1306_ui_text(16, 62, "MID AGAIN", true);
        ssd1306_ui_text(16, 112, "DN EXIT", true);
    }
    ssd1306_show();
}

static void beam_arm(void) { beam_state = BEAM_WAITING; beam_arm_at = make_timeout_time_ms(2500); beam_samples = beam_crossings = 0; beam_baseline[0]=beam_baseline[1]=beam_baseline[2]=0; }
static void beam_process_sample(void) {
    if (beam_state == BEAM_WAITING) { if (!time_reached(beam_arm_at)) return; beam_state = BEAM_LISTENING; beam_samples = 0; return; }
    if (beam_state != BEAM_LISTENING) return;
    float v[3] = {(float)live_accel_x,(float)live_accel_y,(float)live_accel_z};
    if (beam_samples < 30u) { for(unsigned i=0;i<3;i++) beam_baseline[i] += v[i]/30.0f; if (++beam_samples==30u) { beam_axis=0; } return; }
    float d[3]={v[0]-beam_baseline[0],v[1]-beam_baseline[1],v[2]-beam_baseline[2]};
    if (beam_samples==30u) { for(unsigned i=1;i<3;i++) if(fabsf(d[i])>fabsf(d[beam_axis])) beam_axis=i; beam_previous=d[beam_axis]; beam_current=d[beam_axis]; ++beam_samples; return; }
    float next=d[beam_axis];
    if (beam_current>0 && beam_previous<=0 && fabsf(beam_current)>80.0f) { uint32_t t=to_ms_since_boot(get_absolute_time()); if(!beam_crossings) beam_first_cross=t; beam_last_cross=t; ++beam_crossings; }
    beam_previous=beam_current; beam_current=next; ++beam_samples;
    if (beam_samples >= 270u) beam_state=BEAM_RESULT;
}
static void show_beam(void) {
    if(!oled_ok)return; char t[12]; ui_begin("BEAM","I");
    if(beam_state==BEAM_READY){ssd1306_ui_text(16,32,"CLAMP",true);ssd1306_ui_text(16,46,"MID ARM",true);}
    else if(beam_state==BEAM_WAITING){ssd1306_ui_text(16,32,"SET UP",true);ssd1306_ui_text(16,47,"AUTO ARM",true);}
    else if(beam_state==BEAM_LISTENING){ssd1306_ui_text(16,32,"TAP BEAM",true);ssd1306_ui_text(16,47,"LISTEN",true);}
    else { float hz=beam_crossings>1 ? 1000.0f*(beam_crossings-1u)/(beam_last_cross-beam_first_cross) : 0; uint32_t h=(uint32_t)(hz*10+.5f); snprintf(t,sizeof t,"HZ %lu.%lu",(unsigned long)(h/10),(unsigned long)(h%10)); ssd1306_ui_text(16,36,t,true); ssd1306_ui_text(16,52,"MID AGAIN",true); } ssd1306_show();
}

static void show_record_menu(void) {
    if (!oled_ok) return;
    ui_begin("RECORD", "I");
    ui_menu_item(25, "DROP", ui_selection == 0);
    ui_menu_item(38, "BACK", ui_selection == 1);
    ssd1306_ui_text(18, 112, "MID GO", true);
    ssd1306_show();
}

static void show_drop_test_menu(void) {
    if (!oled_ok) return;
    ui_begin("DROP", "I");
    ui_menu_item(25, "ARM", ui_selection == 0);
    ui_menu_item(38, "BACK", ui_selection == 1);
    ssd1306_ui_text(18, 112, "MID GO", true);
    ssd1306_show();
}

static void show_challenges_menu(void) {
    if (!oled_ok) return;
    ui_begin("GAMES", "I");
    ui_menu_item(25, "GENTLE", ui_selection == 0);
    ui_menu_item(38, "BACK", ui_selection == 1);
    ssd1306_ui_text(18, 112, "MID GO", true);
    ssd1306_show();
}

static void show_gentle_ready(void) {
    if (!oled_ok) return;
    ui_begin("CATCH", "G");
    ssd1306_ui_text(18, 36, "READY", true);
    ssd1306_ui_text(18, 48, "THROW", true);
    ssd1306_show();
}

static void show_gentle_fall(void) {
    if (!oled_ok) return;
    ui_begin("CATCH", "G");
    ssd1306_ui_text(18, 36, "FALL", true);
    ssd1306_ui_text(18, 48, "CATCH", true);
    ssd1306_show();
}

static void show_gentle_measure(void) {
    if (!oled_ok) return;
    ui_begin("CATCH", "G");
    ssd1306_ui_text(18, 36, "MEASURE", true);
    ssd1306_show();
}

static void show_gentle_result(void) {
    if (!oled_ok) return;
    char peak[8];
    gentle_score_t score = gentle_saturated ? GENTLE_SCORE_HARD :
                           gentle_peak_axis_counts >= GENTLE_HARD_MIN_COUNTS ? GENTLE_SCORE_HARD :
                           gentle_peak_axis_counts >= GENTLE_FIRM_MIN_COUNTS ? GENTLE_SCORE_FIRM :
                           GENTLE_SCORE_GENTLE;
    ui_begin(gentle_saturated ? "CLIPPED" :
             score == GENTLE_SCORE_GENTLE ? "GENTLE" :
             score == GENTLE_SCORE_FIRM ? "FIRM" : "HARD", "G");
    if (gentle_saturated) {
        ssd1306_ui_text(18, 36, "PEAK", true);
        ssd1306_ui_text_scaled(24, 48, "16", true, 2);
        ssd1306_ui_text_scaled(20, 70, "G+", true, 2);
    } else {
        snprintf(peak, sizeof peak, "%lu.%lu",
                 (unsigned long)(gentle_peak_axis_counts / ACCEL_COUNTS_PER_G),
                 (unsigned long)((gentle_peak_axis_counts % ACCEL_COUNTS_PER_G) * 10u / ACCEL_COUNTS_PER_G));
        ssd1306_ui_text(18, 36, "PEAK", true);
        // Four two-times glyphs fit from x=17 through x=62, entirely to the
        // right of the yellow status rail.  The unit gets its own large line.
        ssd1306_ui_text_scaled(17, 48, peak, true, 2);
        ssd1306_ui_text_scaled(26, 70, "G", true, 2);
    }
    ssd1306_ui_text(18, 112, "MID EXIT", true);
    ssd1306_show();
}

static void show_capture_wait_still(void) {
    if (!oled_ok) return;
    ui_begin("RECORD", "A");
    ssd1306_ui_text(18, 34, "HOLD", true);
    ssd1306_ui_text(18, 46, "STILL", true);
    ssd1306_ui_text(18, 112, "ARMING", true);
    ssd1306_show();
}

static void show_capture_armed(void) {
    if (!oled_ok) return;
    ui_begin("READY", "R");
    ssd1306_ui_text(18, 36, "WAIT", true);
    ssd1306_ui_text(18, 48, "DROP", true);
    ssd1306_show();
}

static void show_capture_event(void) {
    if (!oled_ok) return;
    ui_begin(trigger_was_freefall ? "FALL" : "IMPACT", "E");
    ssd1306_ui_text(18, 36, "EVENT", true);
    ssd1306_ui_text(18, 48, "FOUND", true);
    ssd1306_ui_text(18, 112, "SAVING", true);
    ssd1306_show();
}

static void show_capture_complete(void) {
    if (!oled_ok) return;
    ui_begin("DONE", "D");
    ssd1306_ui_text(18, 36, "USB", true);
    ssd1306_ui_text(18, 48, "DL", true);
    ssd1306_ui_text(18, 112, "MID OPT", true);
    ssd1306_show();
}

static void show_capture_options(void) {
    if (!oled_ok) return;
    ui_begin("SAVED", "D");
    ui_menu_item(25, "ERASE", ui_selection == 0);
    ui_menu_item(38, "BACK", ui_selection == 1);
    ssd1306_ui_text(18, 112, "MID GO", true);
    ssd1306_show();
}

static void show_erase_confirm(void) {
    if (!oled_ok) return;
    ui_begin("ERASE", "D");
    ui_menu_item(25, "NO", ui_selection == 0);
    ui_menu_item(38, "YES", ui_selection == 1);
    ssd1306_ui_text(18, 112, "MID GO", true);
    ssd1306_show();
}

static void calibration_reset_wizard(void) {
    calibration_faces_seen = 0;
    calibration_collecting = false;
    calibration_armed = false;
    calibration_complete = false;
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
    if (!oled_ok) return;
    ui_begin("CAL", "I");
    char progress[10];
    snprintf(progress, sizeof progress, "FACES %u/6",
             (unsigned)__builtin_popcount(calibration_faces_seen));
    ssd1306_ui_text(18, 25, progress, true);
    if (calibration_complete) {
        ssd1306_ui_text(18, 38, "SAVED", true);
        ssd1306_ui_text(18, 112, "MID EXIT", true);
    } else if (calibration_armed) {
        ssd1306_ui_text(18, 45, "SET DOWN", true);
        ssd1306_ui_text(18, 59, "AUTO CAP", true);
    } else if (calibration_collecting) {
        ssd1306_ui_text(18, 45, "HOLD", true);
        ssd1306_ui_text(18, 59, "STILL", true);
    } else if (calibration_feedback[0]) {
        ssd1306_ui_text(18, 45, calibration_feedback, true);
        ssd1306_ui_text(18, 59, "NEW FACE", true);
        ssd1306_ui_text(18, 112, "MID CAP", true);
    } else {
        ssd1306_ui_text(18, 45, "PLACE", true);
        ssd1306_ui_text(18, 59, "ANY FACE", true);
        ssd1306_ui_text(18, 112, "MID CAP", true);
    }
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
        if ((int32_t)calibration_maximum[i] - calibration_minimum[i] > CALIBRATION_STILL_RANGE) {
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
    for (unsigned i = 0; i < 3u; ++i) {
        int32_t span = (int32_t)calibration_positive[i] - calibration_negative[i];
        if (span < (int32_t)(LIVE_ACCEL_COUNTS_PER_G * 16u / 10u)) {
            strcpy(calibration_feedback, "BAD RANGE");
            calibration_faces_seen = 0;
            return;
        }
        calibration.offset_live_counts[i] = ((int32_t)calibration_positive[i] + calibration_negative[i]) / 2;
        calibration.scale_q16[i] = (uint32_t)(((uint64_t)2u * LIVE_ACCEL_COUNTS_PER_G << 16) / span);
    }
    if (calibration_save()) calibration_complete = true;
    else strcpy(calibration_feedback, "SAVE FAIL");
}

static void calibration_process_raw(int16_t x, int16_t y, int16_t z) {
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

static void show_ui(void) {
    switch (ui_state) {
    case UI_HOME: show_home(); break;
    case UI_RECORD: show_record_menu(); break;
    case UI_DROP_TEST: show_drop_test_menu(); break;
    case UI_CHALLENGES: show_challenges_menu(); break;
    case UI_ACCELERATION: show_acceleration(); break;
    case UI_GYRO: show_gyro(); break;
    case UI_RAW_XYZ: show_raw_xyz(); break;
    case UI_INCLINOMETER: show_inclinometer(); break;
    case UI_CALIBRATION: show_calibration(); break;
    case UI_FRICTION: show_friction(); break;
    case UI_PENDULUM: show_pendulum(); break;
    case UI_BEAM: show_beam(); break;
    case UI_GENTLE_CATCH:
        if (gentle_result_available) show_gentle_result();
        else show_gentle_ready();
        break;
    case UI_CAPTURE_COMPLETE: show_capture_complete(); break;
    case UI_CAPTURE_OPTIONS: show_capture_options(); break;
    case UI_ERASE_CONFIRM: show_erase_confirm(); break;
    }
}

static void sample_power(void) {
    uint32_t total = 0;
    for (unsigned i = 0; i < BATTERY_ADC_SAMPLES; ++i) total += adc_read();
    // 3.3 V reference, 12-bit ADC, and a 2:1 divider from Out+ to ADC3.
    uint16_t measured_mv = (uint16_t)(((total / BATTERY_ADC_SAMPLES) * 6600u + 2047u) / 4095u);
    bool previous_usb_state = usb_power_present;
    power_out_millivolts = measured_mv;
    if (usb_power_present) usb_power_present = measured_mv >= USB_PRESENT_CLEAR_MV;
    else usb_power_present = measured_mv > USB_PRESENT_SET_MV;
    if (oled_ok && previous_usb_state != usb_power_present) show_ui();
}

static const char *capture_state_name(void) {
    switch (capture_state) {
    case CAPTURE_WAIT_STILL: return "ARMING - HOLD STILL";
    case CAPTURE_WAIT_EVENT: return "ARMED - WAITING FOR EVENT";
    case CAPTURE_POST_EVENT: return "EVENT - POST CAPTURE";
    case CAPTURE_COMPLETE: return "COMPLETE";
    case CAPTURE_GENTLE_WAIT_FALL: return "GENTLE CATCH - READY";
    case CAPTURE_GENTLE_WAIT_CATCH: return "GENTLE CATCH - FREEFALL";
    case CAPTURE_GENTLE_MEASURE: return "GENTLE CATCH - MEASURING";
    case CAPTURE_GENTLE_RESULT: return "GENTLE CATCH - RESULT";
    default: return "IDLE";
    }
}

static void print_capture_status(void) {
    printf("Capture: %s, %lu/%u ring samples, FIFO overrun: %s",
           capture_state_name(),
           (unsigned long)capture_count, CAPTURE_MAX_SAMPLES,
           capture_fifo_overrun ? "YES" : "no");
    if (trigger_detected)
        printf(", trigger: %s at ring index %lu",
               trigger_was_freefall ? "FREEFALL" : "IMPACT",
               (unsigned long)trigger_index);
    printf("\n");
}

static size_t capture_first_index(void) {
    return capture_oldest_index;
}

static const lsm6dsv_accel_sample_t *capture_sample_at(size_t chronological_index) {
    size_t physical_index = (capture_first_index() + chronological_index) % CAPTURE_MAX_SAMPLES;
    return &capture_samples[physical_index];
}

static uint32_t crc32_byte(uint32_t crc, uint8_t value) {
    crc ^= value;
    for (unsigned bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
    return crc;
}

static uint32_t capture_payload_crc32(void) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < capture_count; ++i) {
        const lsm6dsv_accel_sample_t *sample = capture_sample_at(i);
        uint16_t values[] = { (uint16_t)sample->x, (uint16_t)sample->y, (uint16_t)sample->z };
        for (unsigned axis = 0; axis < 3; ++axis) {
            crc = crc32_byte(crc, (uint8_t)values[axis]);
            crc = crc32_byte(crc, (uint8_t)(values[axis] >> 8));
        }
    }
    return ~crc;
}

static void write_u8(uint8_t value) { putchar_raw(value); }

static void write_u16(uint16_t value) {
    write_u8((uint8_t)value);
    write_u8((uint8_t)(value >> 8));
}

static void write_u32(uint32_t value) {
    write_u16((uint16_t)value);
    write_u16((uint16_t)(value >> 16));
}

static void download_capture(void) {
    if (!capture_complete) {
        printf("ERROR: capture is not complete; finish or stop it before download.\n");
        return;
    }

    uint8_t flags = 0;
    if (trigger_detected) flags |= EGG_FLAG_TRIGGERED;
    if (trigger_was_freefall) flags |= EGG_FLAG_FREEFALL;
    if (capture_fifo_overrun) flags |= EGG_FLAG_FIFO_OVERRUN;
    uint32_t trigger_offset = 0xffffffffu;
    if (trigger_detected) {
        size_t first = capture_first_index();
        trigger_offset = (uint32_t)((trigger_index + CAPTURE_MAX_SAMPLES - first) % CAPTURE_MAX_SAMPLES);
    }
    uint32_t payload_crc = capture_payload_crc32();

    // The text line lets a human terminal know what is about to happen. Browser
    // clients resynchronize on the EGG1 binary magic that immediately follows.
    printf("DOWNLOAD %lu SAMPLES - BINARY FOLLOWS\n", (unsigned long)capture_count);
    stdio_flush();

    write_u8('E'); write_u8('G'); write_u8('G'); write_u8('1');
    write_u8(EGG_FILE_VERSION);
    write_u8(flags);
    write_u16(EGG_FILE_HEADER_BYTES);
    write_u32(CAPTURE_RATE_HZ);
    write_u32((uint32_t)capture_count);
    write_u32(trigger_offset);
    write_u32(post_trigger_samples);
    write_u16(ACCEL_COUNTS_PER_G);
    write_u16(EGG_FILE_SAMPLE_BYTES);
    write_u32(payload_crc);

    for (size_t i = 0; i < capture_count; ++i) {
        const lsm6dsv_accel_sample_t *sample = capture_sample_at(i);
        write_u16((uint16_t)sample->x);
        write_u16((uint16_t)sample->y);
        write_u16((uint16_t)sample->z);
    }
    stdio_flush();
}

static bool verify_capture_range(void) {
    uint8_t ctrl8 = lsm6dsv_read_ctrl8();
    if (ctrl8 != 0x03u) {
        lsm6dsv_fifo_stop();
        printf("ERROR: IMU CTRL8 readback is 0x%02X; expected 0x03 (+/-16 g).\n", ctrl8);
        return false;
    }
    printf("IMU capture config verified: CTRL8=0x%02X (+/-16 g, 0.488 mg/LSB).\n", ctrl8);
    return true;
}

static void start_capture(void) {
    if (capture_complete) {
        printf("Capture is protected. Erase it from the EggBert menu before rearming.\n");
        return;
    }
    capture_count = 0;
    capture_write_index = 0;
    capture_oldest_index = 0;
    trigger_index = 0;
    still_samples = 0;
    post_trigger_samples = 0;
    capture_fifo_overrun = false;
    capture_complete = false;
    trigger_detected = false;
    trigger_was_freefall = false;
    if (!lsm6dsv_fifo_start()) {
        printf("ERROR: could not start IMU FIFO\n");
        return;
    }
    if (!verify_capture_range()) return;
    capture_active = true;
    capture_state = CAPTURE_WAIT_STILL;
    next_capture_poll = make_timeout_time_ms(5);
    show_capture_wait_still();
    printf("Capture armed from EggBert menu: hold still for one second, then wait for freefall or impact.\n");
}

static void finish_capture(const char *reason) {
    if (!capture_active) return;
    lsm6dsv_fifo_stop();
    capture_active = false;
    capture_complete = true;
    capture_state = CAPTURE_COMPLETE;
    ui_state = UI_CAPTURE_COMPLETE;
    ui_selection = 0;
    show_capture_complete();
    printf("Capture complete (%s). ", reason);
    print_capture_status();
}

static void erase_capture(void) {
    if (capture_active) return;
    capture_count = 0;
    capture_write_index = 0;
    capture_oldest_index = 0;
    capture_complete = false;
    capture_fifo_overrun = false;
    trigger_detected = false;
    trigger_was_freefall = false;
    capture_state = CAPTURE_IDLE;
    ui_state = UI_HOME;
    ui_selection = 0;
    show_ui();
    printf("Capture buffer erased from EggBert menu.\n");
}

// This challenge uses the FIFO for timely samples but deliberately never
// writes the RAM capture buffer. A saved .egg file therefore remains intact.
static void start_gentle_catch(void) {
    gentle_measure_samples = 0;
    gentle_peak_axis_counts = 0;
    gentle_rearm_still_samples = 0;
    gentle_freefall_samples = 0;
    gentle_saturated = false;
    gentle_result_available = false;
    if (!lsm6dsv_fifo_start()) {
        printf("ERROR: could not start IMU FIFO for Gentle Catch.\n");
        return;
    }
    if (!verify_capture_range()) return;
    capture_active = true;
    capture_state = CAPTURE_GENTLE_WAIT_FALL;
    ui_state = UI_GENTLE_CATCH;
    ui_selection = 0;
    next_capture_poll = make_timeout_time_ms(5);
    show_gentle_ready();
    printf("Gentle Catch started: throw EggBert, then catch it gently.\n");
}

static void finish_gentle_catch(void) {
    // Keep sampling after the result is shown. The result remains visible
    // until the next real freefall begins a new catch attempt.
    capture_state = CAPTURE_GENTLE_RESULT;
    gentle_rearm_still_samples = 0;
    gentle_result_available = true;
    gentle_result_led_until = make_timeout_time_ms(GENTLE_RESULT_LED_MS);
    show_gentle_result();
    if (gentle_saturated)
        printf("Gentle Catch result: sensor exceeded its 16 g axis range.\n");
    else
        printf("Gentle Catch result: peak axis %lu.%lu g.\n",
               (unsigned long)(gentle_peak_axis_counts / ACCEL_COUNTS_PER_G),
               (unsigned long)((gentle_peak_axis_counts % ACCEL_COUNTS_PER_G) * 10u / ACCEL_COUNTS_PER_G));
}

static void exit_gentle_catch(void) {
    if (capture_active) lsm6dsv_fifo_stop();
    capture_active = false;
    capture_state = CAPTURE_IDLE;
    ui_state = UI_CHALLENGES;
    ui_selection = 0;
    show_ui();
    printf("Gentle Catch exited.\n");
}

static void update_status_leds(bool imu_ok) {
    if (!imu_ok) { leds(0x04u); return; }
    switch (capture_state) {
    case CAPTURE_WAIT_STILL: leds(0x02u); break;
    case CAPTURE_WAIT_EVENT: leds(0x01u); break;
    case CAPTURE_POST_EVENT: leds(0x03u); break;
    case CAPTURE_COMPLETE: leds(0x04u); break;
    case CAPTURE_GENTLE_WAIT_FALL: {
        unsigned mask = 0x01u; // Green always means rearmed and ready to throw.
        bool show_result_led = gentle_result_available &&
            absolute_time_diff_us(get_absolute_time(), gentle_result_led_until) > 0;
        bool blink_on = (to_ms_since_boot(get_absolute_time()) / 300u) % 2u == 0u;
        if (show_result_led) {
            if (gentle_saturated || gentle_peak_axis_counts >= GENTLE_HARD_MIN_COUNTS)
                mask |= blink_on ? 0x04u : 0u;
            else if (gentle_peak_axis_counts >= GENTLE_FIRM_MIN_COUNTS)
                mask |= blink_on ? 0x02u : 0u;
            else mask |= 0x02u;
        }
        leds(mask);
        break;
    }
    case CAPTURE_GENTLE_WAIT_CATCH: leds(0); break;
    case CAPTURE_GENTLE_MEASURE: leds(0); break;
    case CAPTURE_GENTLE_RESULT: {
        bool show_result_led = absolute_time_diff_us(get_absolute_time(), gentle_result_led_until) > 0;
        bool blink_on = (to_ms_since_boot(get_absolute_time()) / 300u) % 2u == 0u;
        if (!show_result_led) leds(0);
        else if (gentle_saturated || gentle_peak_axis_counts >= GENTLE_HARD_MIN_COUNTS)
            leds(blink_on ? 0x04u : 0u);
        else if (gentle_peak_axis_counts >= GENTLE_FIRM_MIN_COUNTS)
            leds(blink_on ? 0x02u : 0u);
        else leds(0x02u);
        break;
    }
    default: leds(0); break;
    }
}

static uint64_t magnitude_squared(const lsm6dsv_accel_sample_t *sample) {
    int64_t x = sample->x;
    int64_t y = sample->y;
    int64_t z = sample->z;
    return (uint64_t)(x * x + y * y + z * z);
}

static bool magnitude_between(uint64_t magnitude, uint32_t minimum, uint32_t maximum) {
    uint64_t minimum_squared = (uint64_t)minimum * minimum;
    uint64_t maximum_squared = (uint64_t)maximum * maximum;
    return magnitude >= minimum_squared && magnitude <= maximum_squared;
}

static void store_capture_sample(const lsm6dsv_accel_sample_t *sample) {
    capture_samples[capture_write_index] = *sample;
    capture_write_index = (capture_write_index + 1u) % CAPTURE_MAX_SAMPLES;
    if (capture_count < CAPTURE_MAX_SAMPLES) ++capture_count;
    else capture_oldest_index = (capture_oldest_index + 1u) % CAPTURE_MAX_SAMPLES;
}

static void retain_pretrigger_window(void) {
    // Keep exactly the rolling half-second before the trigger plus its sample.
    // This discards the menu/arming delay without copying the large RAM buffer.
    size_t keep = PRE_TRIGGER_SAMPLES + 1u;
    if (capture_count < keep) keep = capture_count;
    capture_oldest_index = (capture_write_index + CAPTURE_MAX_SAMPLES - keep) % CAPTURE_MAX_SAMPLES;
    capture_count = keep;
}

static uint32_t sample_peak_axis(const lsm6dsv_accel_sample_t *sample) {
    int32_t x = sample->x;
    int32_t y = sample->y;
    int32_t z = sample->z;
    uint32_t peak = (uint32_t)(x < 0 ? -x : x);
    uint32_t axis = (uint32_t)(y < 0 ? -y : y);
    if (axis > peak) peak = axis;
    axis = (uint32_t)(z < 0 ? -z : z);
    return axis > peak ? axis : peak;
}

static void process_gentle_sample(const lsm6dsv_accel_sample_t *sample) {
    uint64_t magnitude = magnitude_squared(sample);
    uint32_t peak_axis = sample_peak_axis(sample);

    if (capture_state == CAPTURE_GENTLE_WAIT_FALL) {
        if (magnitude < (uint64_t)FREEFALL_MAX_COUNTS * FREEFALL_MAX_COUNTS) {
            if (++gentle_freefall_samples >= GENTLE_FREEFALL_SAMPLES) {
                capture_state = CAPTURE_GENTLE_WAIT_CATCH;
                gentle_result_available = false;
                show_gentle_fall();
                printf("Gentle Catch: 100 ms freefall confirmed; waiting for catch.\n");
            }
        } else gentle_freefall_samples = 0;
        return;
    }

    if (capture_state == CAPTURE_GENTLE_RESULT) {
        if (magnitude_between(magnitude, STILL_MIN_COUNTS, STILL_MAX_COUNTS)) {
            if (++gentle_rearm_still_samples >= GENTLE_REARM_STILL_SAMPLES) {
                capture_state = CAPTURE_GENTLE_WAIT_FALL;
                gentle_freefall_samples = 0;
                // Leave the previous score onscreen. Green now means it is
                // armed for the next throw; a confirmed freefall clears it.
                printf("Gentle Catch rearmed: held still for 0.5 seconds; green means ready to throw.\n");
            }
        } else gentle_rearm_still_samples = 0;
        return;
    }

    if (capture_state == CAPTURE_GENTLE_WAIT_CATCH) {
        if (magnitude >= (uint64_t)GENTLE_CATCH_START_COUNTS * GENTLE_CATCH_START_COUNTS) {
            capture_state = CAPTURE_GENTLE_MEASURE;
            gentle_measure_samples = 1;
            gentle_peak_axis_counts = peak_axis;
            gentle_saturated = peak_axis >= SATURATION_NEAR_COUNTS;
            show_gentle_measure();
        }
        return;
    }

    if (capture_state == CAPTURE_GENTLE_MEASURE) {
        if (peak_axis > gentle_peak_axis_counts) gentle_peak_axis_counts = peak_axis;
        if (peak_axis >= SATURATION_NEAR_COUNTS) gentle_saturated = true;
        if (++gentle_measure_samples >= GENTLE_CATCH_SAMPLES) finish_gentle_catch();
    }
}

static void process_capture_sample(const lsm6dsv_accel_sample_t *sample) {
    if (capture_state >= CAPTURE_GENTLE_WAIT_FALL) {
        process_gentle_sample(sample);
        return;
    }
    store_capture_sample(sample);
    uint64_t magnitude = magnitude_squared(sample);

    if (capture_state == CAPTURE_WAIT_STILL) {
        if (magnitude_between(magnitude, STILL_MIN_COUNTS, STILL_MAX_COUNTS)) {
            if (++still_samples >= STILL_REQUIRED_SAMPLES) {
                capture_state = CAPTURE_WAIT_EVENT;
                show_capture_armed();
                printf("Capture armed: stable for one second; waiting for event.\n");
            }
        } else {
            still_samples = 0;
        }
        return;
    }

    if (capture_state == CAPTURE_WAIT_EVENT) {
        if (magnitude < (uint64_t)FREEFALL_MAX_COUNTS * FREEFALL_MAX_COUNTS) {
            trigger_detected = true;
            trigger_was_freefall = true;
            trigger_index = capture_write_index == 0 ? CAPTURE_MAX_SAMPLES - 1u : capture_write_index - 1u;
            retain_pretrigger_window();
            post_trigger_samples = 0;
            capture_state = CAPTURE_POST_EVENT;
            show_capture_event();
            printf("Freefall trigger: keeping %u pre-trigger and %u post-trigger samples.\n",
                   PRE_TRIGGER_SAMPLES, POST_TRIGGER_SAMPLES);
        }
        return;
    }

    if (capture_state == CAPTURE_POST_EVENT && ++post_trigger_samples >= POST_TRIGGER_SAMPLES)
        finish_capture("post-event window complete");
}

static void service_capture(void) {
    if (!capture_active) return;

    // The watermark IRQ is the normal service signal. A 5 ms poll is only a
    // backstop for a final fragment that never reaches the 32-sample watermark.
    if (!lsm6dsv_fifo_irq_pending() &&
        absolute_time_diff_us(get_absolute_time(), next_capture_poll) > 0)
        return;
    next_capture_poll = make_timeout_time_ms(5);

    while (capture_active) {
        bool overrun = false;
        size_t read = lsm6dsv_fifo_read(fifo_samples, 64u, &overrun);
        if (overrun) capture_fifo_overrun = true;
        if (read == 0) break;
        for (size_t i = 0; i < read && capture_active; ++i) {
            calibration_apply_capture_sample(&fifo_samples[i]);
            process_capture_sample(&fifo_samples[i]);
        }
    }
}

static void stop_orientation(void) {
    orientation_stream = false;
    orientation_fused = false;
    lsm6dsv_orientation_stop();
}

static void service_orientation(void) {
    if (!orientation_fused) return;
    if (!stdio_usb_connected()) { stop_orientation(); return; }
    lsm6dsv_quaternion_t q;
    bool overrun = false;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool fresh = lsm6dsv_orientation_read(&q, &overrun);
    if (overrun || now - orientation_last_ms > 1500u) {
        stop_orientation();
        printf("ERROR: orientation %s\n", overrun ? "FIFO overflow" : "sensor timeout");
    } else if (fresh) {
        orientation_last_ms = now;
        printf("Q1,%lu,%ld,%ld,%ld,%ld\n", (unsigned long)now,
            (long)lroundf(q.w * 1000000.0f), (long)lroundf(q.x * 1000000.0f),
            (long)lroundf(q.y * 1000000.0f), (long)lroundf(q.z * 1000000.0f));
    }
}

static void handle_serial_command(void) {
    static char command[48];
    static size_t length;
    int character;
    while ((character = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (character == '\r' || character == '\n') {
            if (length == 0) continue;
            command[length] = '\0';
            if (strcmp(command, "arm") == 0 || strcmp(command, "start") == 0) {
                printf("ERROR: arm EggBert using its on-device menu.\n");
            } else if (strcmp(command, "stop") == 0) {
                printf("ERROR: capture control is on EggBert; serial stop is disabled.\n");
            } else if (strcmp(command, "status") == 0) {
                print_capture_status();
            } else if (strcmp(command, "download") == 0) {
                stop_orientation();
                download_capture();
            } else if (strcmp(command, "orientation fused") == 0) {
                if (capture_active || ui_state != UI_HOME) {
                    printf("ERROR: return EggBert to HOME before orientation.\n");
                } else {
                    stop_orientation();
                    orientation_fused = lsm6dsv_orientation_start();
                    orientation_last_ms = to_ms_since_boot(get_absolute_time());
                    printf(orientation_fused ? "ORIENTATION Q1 60HZ SCALE 1000000\n" : "ERROR: fusion setup failed\n");
                }
            } else if (strcmp(command, "orientation on") == 0) {
                stop_orientation();
                if (!capture_active) { orientation_stream = true; printf("ORIENTATION +Y HOME\n"); }
                else printf("ERROR: capture is active.\n");
            } else if (strcmp(command, "orientation off") == 0) {
                stop_orientation();
            } else if (strcmp(command, "clear") == 0) {
                printf("ERROR: erase captures using the EggBert menu.\n");
            } else if (strcmp(command, "help") == 0) {
                printf("Commands: status, download, orientation fused/on/off, help. Capture control uses the EggBert menu.\n");
            } else {
                printf("Unknown command: %s (type help)\n", command);
            }
            length = 0;
        } else if (character >= 32 && character < 127) {
            if (length < sizeof command - 1u) command[length++] = (char)character;
        }
    }
}

static void handle_button_press(unsigned button) {
    if (orientation_fused) { stop_orientation(); printf("ORIENTATION STOPPED: device menu\n"); }
    if (capture_active) {
        if (ui_state == UI_GENTLE_CATCH && button == 1) exit_gentle_catch();
        return;
    }

    if (ui_state == UI_GYRO) {
        if (button == 0) gyro_peak_view = !gyro_peak_view;
        else if (button == 1 && gyro_ok)
            gyro_session_reset(&gyro_session, to_ms_since_boot(get_absolute_time()));
        else if (button == 2) {
            lsm6dsv_gyro_stop();
            ui_state = UI_HOME;
            ui_selection = 9;
        }
        show_ui();
        return;
    }

    if (capture_state == CAPTURE_GENTLE_RESULT) {
        if (button == 1) exit_gentle_catch();
        return;
    }

    if (ui_state == UI_FRICTION) {
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
                    ui_state = UI_HOME;
                    ui_selection = 6;
                }
            }
            show_ui();
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
            ui_state = UI_HOME;
            ui_selection = 6;
        }
        show_ui();
        return;
    }

    if (ui_state == UI_PENDULUM) {
        if (button == 1) {
            if (pendulum_state == PENDULUM_READY || pendulum_state == PENDULUM_RESULT) pendulum_arm();
            else pendulum_state = PENDULUM_READY;
        } else if (button == 2 && pendulum_state == PENDULUM_RESULT) {
            ui_state = UI_HOME;
            ui_selection = 2;
        } else if (button == 0 && pendulum_state != PENDULUM_MEASURING) {
            ui_state = UI_HOME;
            ui_selection = 2;
        }
        show_ui();
        return;
    }
    if (ui_state == UI_BEAM) {
        if (button == 1) { if (beam_state == BEAM_READY || beam_state == BEAM_RESULT) beam_arm(); else beam_state = BEAM_READY; }
        else if (button == 0 && beam_state != BEAM_LISTENING) { ui_state = UI_HOME; ui_selection = 6; }
        show_ui(); return;
    }

    if (button == 0) {
        if (ui_state == UI_HOME) { ui_selection = home_step(false); show_ui(); return; }
        unsigned count =
                         ((ui_state == UI_ACCELERATION || ui_state == UI_RAW_XYZ || ui_state == UI_INCLINOMETER || ui_state == UI_CALIBRATION) ? 1u : 2u);
        ui_selection = (ui_selection + count - 1u) % count;
        show_ui();
        return;
    }
    if (button == 2) {
        if (ui_state == UI_HOME) { ui_selection = home_step(true); show_ui(); return; }
        unsigned count =
                         ((ui_state == UI_ACCELERATION || ui_state == UI_RAW_XYZ || ui_state == UI_INCLINOMETER || ui_state == UI_CALIBRATION) ? 1u : 2u);
        ui_selection = (ui_selection + 1u) % count;
        show_ui();
        return;
    }
    if (button != 1) return;

    switch (ui_state) {
    case UI_HOME:
        if (ui_selection == 0) { ui_state = UI_RECORD; ui_selection = 0; }
        else if (ui_selection == 1) { ui_state = UI_CHALLENGES; ui_selection = 0; }
        else if (ui_selection == 2) { pendulum_state = PENDULUM_READY; ui_state = UI_PENDULUM; ui_selection = 0; }
        else if (ui_selection == 3) { ui_state = UI_ACCELERATION; ui_selection = 0; }
        else if (ui_selection == 4) { ui_state = UI_INCLINOMETER; ui_selection = 0; }
        else if (ui_selection == 5) { friction_reset_session(); ui_state = UI_FRICTION; ui_selection = 0; }
        else if (ui_selection == 6) { beam_state = BEAM_READY; ui_state = UI_BEAM; ui_selection = 0; }
        else if (ui_selection == 7) { ui_state = UI_RAW_XYZ; ui_selection = 0; }
        else if (ui_selection == 9) { ui_state = UI_GYRO; ui_selection = 0; gyro_begin(); }
        else { calibration_reset_wizard(); ui_state = UI_CALIBRATION; ui_selection = 0; }
        break;
    case UI_RECORD:
        if (ui_selection == 0) { ui_state = UI_DROP_TEST; ui_selection = 0; }
        else { ui_state = UI_HOME; ui_selection = 0; }
        break;
    case UI_DROP_TEST:
        if (ui_selection == 0) start_capture();
        else { ui_state = UI_RECORD; ui_selection = 0; }
        break;
    case UI_CHALLENGES:
        if (ui_selection == 0) start_gentle_catch();
        else { ui_state = UI_HOME; ui_selection = 0; }
        break;
    case UI_INCLINOMETER:
        ui_state = UI_HOME;
        ui_selection = 0;
        break;
    case UI_ACCELERATION:
        ui_state = UI_HOME;
        ui_selection = 3;
        break;
    case UI_GYRO:
        break; // all gyro buttons are handled above
    case UI_RAW_XYZ:
        ui_state = UI_HOME;
        ui_selection = 7;
        break;
    case UI_CALIBRATION:
        if (calibration_complete) { ui_state = UI_HOME; ui_selection = 8; }
        else calibration_begin_face();
        break;
    case UI_CAPTURE_COMPLETE:
        ui_state = UI_CAPTURE_OPTIONS;
        ui_selection = 0;
        break;
    case UI_CAPTURE_OPTIONS:
        if (ui_selection == 0) { ui_state = UI_ERASE_CONFIRM; ui_selection = 0; }
        else { ui_state = UI_CAPTURE_COMPLETE; ui_selection = 0; }
        break;
    case UI_ERASE_CONFIRM:
        if (ui_selection == 0) { ui_state = UI_CAPTURE_OPTIONS; ui_selection = 0; }
        else erase_capture();
        break;
    }
    if (!capture_active) show_ui();
}

int main(void) {
    stdio_init_all();
    setup_gpio();
    sleep_ms(1500);
    printf("EggBert RP2354A bring-up starting\n");

    for (unsigned i = 0; i < 3; ++i) { leds(1u << i); sleep_ms(300); }
    leds(0);

    oled_ok = ssd1306_init();
    printf("OLED at 0x%02X: %s\n", OLED_I2C_ADDRESS, oled_ok ? "OK" : "NOT FOUND");
    sample_power();
    bool imu_ok = lsm6dsv_init();
    calibration_load();
    printf("LSM6DSV SPI WHO_AM_I: %s\n", imu_ok ? "0x70 OK" : "NOT FOUND");
    if (oled_ok) {
        ui_begin("BOOT", imu_ok ? "I" : "X");
        ssd1306_ui_text(18, 36, imu_ok ? "IMU OK" : "IMU BAD", true);
        ssd1306_show();
    }

    if (imu_ok) {
        show_ui();
        printf("Capture bring-up ready. Use the EggBert menu to arm a drop test; type help for serial diagnostics.\n");
    }

    unsigned stable_pressed = 0;
    unsigned last_raw_pressed = 0;
    absolute_time_t buttons_changed_at = get_absolute_time();
    absolute_time_t next_imu_report = make_timeout_time_ms(250);
    absolute_time_t next_power_sample = make_timeout_time_ms(500);
    while (true) {
        handle_serial_command();
        service_capture();
        service_gyro();
        service_orientation();
        if (!capture_active && absolute_time_diff_us(get_absolute_time(), next_power_sample) <= 0) {
            sample_power();
            next_power_sample = make_timeout_time_ms(500);
        }
        unsigned pressed = 0;
        for (unsigned i = 0; i < 3; ++i)
            if (!gpio_get(BUTTONS[i])) pressed |= 1u << i;
        if (pressed != last_raw_pressed) {
            printf("Buttons: UP=%u MID=%u DOWN=%u\n", pressed & 1u, (pressed >> 1) & 1u, (pressed >> 2) & 1u);
            last_raw_pressed = pressed;
            buttons_changed_at = get_absolute_time();
        }
        if (pressed != stable_pressed &&
            absolute_time_diff_us(buttons_changed_at, get_absolute_time()) >= 25000) {
            unsigned new_presses = pressed & ~stable_pressed;
            stable_pressed = pressed;
            for (unsigned i = 0; i < 3; ++i)
                if (new_presses & (1u << i)) handle_button_press(i);
        }
        update_status_leds(imu_ok);
        if (imu_ok && !capture_active && !orientation_fused && absolute_time_diff_us(get_absolute_time(), next_imu_report) <= 0) {
            int16_t x, y, z;
            if (lsm6dsv_read_accel(&x, &y, &z)) {
                calibration_process_raw(x, y, z);
                live_accel_x = calibration_correct_axis(x, 0u, LIVE_ACCEL_COUNTS_PER_G);
                live_accel_y = calibration_correct_axis(y, 1u, LIVE_ACCEL_COUNTS_PER_G);
                live_accel_z = calibration_correct_axis(z, 2u, LIVE_ACCEL_COUNTS_PER_G);
                if (orientation_stream) printf("O,%d,%d,%d\n", live_accel_x, live_accel_y, live_accel_z);
                update_level_filter();
                friction_process_sample();
                pendulum_process_sample();
                beam_process_sample();
                printf("Accel raw: X=%d Y=%d Z=%d (0.061 mg/LSB)\n", x, y, z);
                if (ui_state == UI_ACCELERATION) show_acceleration();
                else if (ui_state == UI_RAW_XYZ) show_raw_xyz();
                else if (ui_state == UI_INCLINOMETER && time_reached(next_level_display)) {
                    show_inclinometer();
                    next_level_display = make_timeout_time_ms(LEVEL_DISPLAY_PERIOD_MS);
                }
                else if (ui_state == UI_CALIBRATION) show_calibration();
                else if (ui_state == UI_FRICTION && time_reached(next_friction_display)) {
                    show_friction();
                    next_friction_display = make_timeout_time_ms(LEVEL_DISPLAY_PERIOD_MS);
                }
                else if (ui_state == UI_PENDULUM) show_pendulum();
                else if (ui_state == UI_BEAM) show_beam();
            }
            next_imu_report = make_timeout_time_ms(
                (ui_state == UI_ACCELERATION || ui_state == UI_RAW_XYZ || ui_state == UI_INCLINOMETER) ? 100 :
                (ui_state == UI_FRICTION && friction_state == FRICTION_ARMED) ? 20 :
                (ui_state == UI_PENDULUM && pendulum_state != PENDULUM_READY) ? 20 :
                (ui_state == UI_BEAM && beam_state != BEAM_READY) ? 10 :
                (ui_state == UI_CALIBRATION && (calibration_collecting || calibration_armed)) ? 50 : 250);
        }
        sleep_ms(capture_active || orientation_fused || ui_state == UI_GYRO ? 1 : 10);
    }
}
