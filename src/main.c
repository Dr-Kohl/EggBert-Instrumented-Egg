// Application coordinator: buttons select activities, sensor services advance
// their state machines, and show_* functions draw the current state. Read
// STUDENT_GUIDE.md before following the recording path through this file.
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
#include "egg_format.h"
#include "accel_select.h"
#include "sound.h"
#include "settings.h"
#include "beam_trigger.h"
#include "beam_frequency.h"
#include "app.h"
#include "app_services.h"
#include "home.h"
#include "ui.h"
#include "calibration.h"
#include "hammer_trigger.h"
#include "idle_power.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "ssd1306.h"

static const uint LEDS[] = {LED_GREEN_PIN, LED_YELLOW_PIN, LED_RED_PIN};
// Physical top, middle, and bottom buttons.  The schematic switch numbering
// runs bottom-to-top, so it is deliberately not the navigation order.
static const uint BUTTONS[] = {BUTTON_3_PIN, BUTTON_2_PIN, BUTTON_1_PIN};

// Sample counts define time in a recording; USB arrival time does not.
// These rates are nominal sensor settings, not a calibrated time reference.
#define CAPTURE_RATE_HZ       3840u
#define CAPTURE_SECONDS         5u
#define CAPTURE_MAX_SAMPLES   (CAPTURE_RATE_HZ * CAPTURE_SECONDS)
#define PRE_TRIGGER_SAMPLES   (CAPTURE_RATE_HZ / 2u)
// The trigger sample itself lies between these two windows.
#define POST_TRIGGER_SAMPLES  (CAPTURE_MAX_SAMPLES - PRE_TRIGGER_SAMPLES - 1u)

// At +/-16 g, the LSM6DSV sensitivity is about 0.488 mg/count.
#define ACCEL_COUNTS_PER_G     2048u
#define STILL_MIN_COUNTS       (ACCEL_COUNTS_PER_G * 8u / 10u)
#define STILL_MAX_COUNTS       (ACCEL_COUNTS_PER_G * 12u / 10u)
#define FREEFALL_MAX_COUNTS    (ACCEL_COUNTS_PER_G * 45u / 100u)
#define STILL_REQUIRED_SAMPLES CAPTURE_RATE_HZ
// Gentle Catch detects freefall with vector magnitude, but scores the largest
// absolute individual axis during a fixed 150 ms window. It does not integrate
// acceleration or estimate collision energy, and a longer catch can outlast it.
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
#define EGG_FLAG_BEAM           0x08u
#define EGG_FLAG_CLIPPED        0x10u

#define BATTERY_ADC_INPUT        3u
#define BATTERY_ADC_SAMPLES      16u
#define USB_PRESENT_SET_MV     4400u
#define USB_PRESENT_CLEAR_MV   4300u

#define CALIBRATION_MAGIC       0x43414C31u /* CAL1 */
#define CALIBRATION_VERSION      1u
#define CALIBRATION_FLASH_BYTES  (2u * FLASH_SECTOR_SIZE)
#define CALIBRATION_FLASH_A      (PICO_FLASH_SIZE_BYTES - CALIBRATION_FLASH_BYTES)
#define CALIBRATION_FLASH_B      (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define LEVEL_FILTER_TIME_MS     200u

// 19,200 samples x 3 axes x 16 bits = 115,200 bytes.  This is intentionally
// RAM-only: recording does not write flash. The second equal-sized array holds
// v2 high-g readings at the SAME ring indices: together they use 230,400 bytes.
// Calibration, unlike recordings, is saved separately in reserved flash sectors.
static lsm6dsv_accel_sample_t capture_samples[CAPTURE_MAX_SAMPLES];
static lsm6dsv_accel_sample_t capture_high_samples[CAPTURE_MAX_SAMPLES];
static lsm6dsv_accel_pair_t fifo_pairs[32];
static lsm6dsv_accel_pair_t capture_raw_pair;
static bool capture_has_highg, capture_dual_failed, capture_diagnostic, capture_diagnostic_roll;
static uint32_t capture_diagnostic_seen;
static lsm6dsv_dual_stats_t capture_dual_stats;
static uint64_t capture_started_us, capture_elapsed_us;
static egg2_metadata_t capture_metadata;
_Static_assert(sizeof(lsm6dsv_accel_sample_t) == 6, "Packed XYZ must be six bytes");
_Static_assert(sizeof capture_samples + sizeof capture_high_samples == 230400,
               "Five-second dual recording buffer budget changed");
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
static bool capture_is_beam;
static bool capture_is_hammer;
static hammer_trigger_t hammer;
static bool hammer_gyro_running;
static uint32_t hammer_gyro_last_ms;
static uint32_t hammer_armed_ms, hammer_tick_ms, hammer_display_ms;
static uint32_t hammer_peak_mg;
static uint32_t hammer_over20_samples;
static bool hammer_limit_unknown;
static bool capture_clipped;
static uint32_t capture_rate = CAPTURE_RATE_HZ;
static uint32_t capture_scale = ACCEL_COUNTS_PER_G;
static uint32_t capture_target = CAPTURE_MAX_SAMPLES;
static uint32_t capture_pretrigger = PRE_TRIGGER_SAMPLES;
static beam_trigger_t beam_trigger;
static uint32_t beam_display_ms, capture_last_sample_ms;
static absolute_time_t next_capture_poll;
static bool oled_ok;
static idle_power_t idle_power;
static bool idle_standby, idle_wake_buttons;
static uint32_t idle_started_ms;
#define IDLE_RECORD_LED_PERIOD_MS 5000u
#define IDLE_RECORD_LED_PULSE_MS   100u
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

typedef enum { BEAM_READY, BEAM_WAITING, BEAM_LISTENING, BEAM_RECORDING, BEAM_RESULT } beam_state_t;
static beam_state_t beam_state;
static absolute_time_t beam_arm_at;
static float beam_result_hz;

static calibration_record_t calibration;
static bool calibration_valid;
const calibration_record_t *calibration_current(void) { return &calibration; }
typedef enum {
    UI_HOME, UI_APP, UI_RECORD, UI_DROP_TEST, UI_CHALLENGES, UI_BEAM,
    UI_GENTLE_CATCH, UI_HAMMER_HIT, UI_CAPTURE_COMPLETE, UI_CAPTURE_OPTIONS, UI_ERASE_CONFIRM,
} ui_state_t;

static ui_state_t ui_state = UI_HOME;
static unsigned ui_selection;

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
static bool gentle_has_highg, gentle_use_high;
static uint32_t gentle_current_peak;
static bool gentle_current_clipped;
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

// A/B flash records let startup choose the newest valid calibration. CRC and
// scale bounds reject damaged records; absence of a valid record means raw data.
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

int16_t calibration_correct_axis(int16_t raw, unsigned axis, uint32_t counts_per_g) {
    if (!calibration_valid) return raw;
    return egg_correct_low(raw, calibration.offset_live_counts[axis], calibration.scale_q16[axis], counts_per_g);
}

static void calibration_apply_capture_sample(lsm6dsv_accel_sample_t *sample) {
    sample->x = calibration_correct_axis(sample->x, 0u, capture_scale);
    sample->y = calibration_correct_axis(sample->y, 1u, capture_scale);
    sample->z = calibration_correct_axis(sample->z, 2u, capture_scale);
}

// Called only by explicit wizard SAVE. Alternate flash sectors so the previous
// valid record survives an interrupted erase/program of the new candidate.
bool calibration_save(const calibration_record_t *candidate) {
    calibration_record_t record = *candidate;
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

static void beam_arm(void);
static void show_beam(void) {
    if (!oled_ok) return;
    char line[12]; ui_begin("BEAM");
    if (beam_state == BEAM_READY) {
        ssd1306_ui_text(2,32,"CLAMP",true);
        ssd1306_ui_text(2,46,"MID ARM",true);
        ssd1306_ui_text(2,62,"480 HZ",true);
        ssd1306_ui_text(2,76,"15 SEC",true);
        ssd1306_ui_text(2,112,"UP HOME",true);
    } else if (beam_state == BEAM_WAITING) {
        ssd1306_ui_text(2,32,"HOLD",true);
        ssd1306_ui_text(2,46,"STILL",true);
        ssd1306_ui_text(2,112,"MID STOP",true);
    } else if (beam_state == BEAM_LISTENING) {
        ssd1306_ui_text(2,32,"READY",true);
        ssd1306_ui_text(2,46,"FLICK",true);
        ssd1306_ui_text(2,62,"0.2 G",true);
        ssd1306_ui_text(2,112,"MID STOP",true);
    } else if (beam_state == BEAM_RECORDING) {
        unsigned remaining = (capture_target-capture_pretrigger-1u-post_trigger_samples+BEAM_RATE_HZ-1u)/BEAM_RATE_HZ;
        snprintf(line,sizeof line,"LEFT %u S",remaining);
        ssd1306_ui_text(2,32,"RECORD",true);
        ssd1306_ui_text(2,48,line,true);
    } else {
        if (beam_result_hz > 0) {
            unsigned hundredths = (unsigned)(beam_result_hz*100+0.5f);
            snprintf(line,sizeof line,"HZ %u.%02u",hundredths/100,hundredths%100);
            ssd1306_ui_text(2,32,line,true);
            ssd1306_ui_text(2,46,"ESTIMATE",true);
        } else {
            ssd1306_ui_text(2,32,"NO CLEAR",true);
            ssd1306_ui_text(2,46,"PERIOD",true);
        }
        if (capture_clipped) ssd1306_ui_text(2,62,"CLIPPED",true);
        ssd1306_ui_text(2,78,"USB DL",true);
        ssd1306_ui_text(2,112,"MID OPT",true);
    }
    ssd1306_show();
}

static void show_record_menu(void) {
    if (!oled_ok) return;
    ui_begin("RECORD");
    ui_menu_item(30, "DROP", ui_selection == 0);
    ui_menu_item(43, "BACK", ui_selection == 1);
    ssd1306_ui_text(2, 112, "MID GO", true);
    ssd1306_show();
}

static void show_drop_test_menu(void) {
    if (!oled_ok) return;
    ui_begin("DROP");
    ui_menu_item(30, "ARM", ui_selection == 0);
    ui_menu_item(43, "BACK", ui_selection == 1);
    ssd1306_ui_text(2, 112, "MID GO", true);
    ssd1306_show();
}

static void show_challenges_menu(void) {
    if (!oled_ok) return;
    ui_begin("CATCH");
    ui_menu_item(30, "GENTLE", ui_selection == 0);
    ui_menu_item(43, "BACK", ui_selection == 1);
    ssd1306_ui_text(2, 112, "MID GO", true);
    ssd1306_show();
}

static void show_gentle_ready(void) {
    if (!oled_ok) return;
    ui_begin("CATCH");
    ssd1306_ui_text(2, 36, "READY", true);
    ssd1306_ui_text(2, 48, "THROW", true);
    ssd1306_show();
}

static void show_gentle_fall(void) {
    if (!oled_ok) return;
    ui_begin("CATCH");
    ssd1306_ui_text(2, 36, "FALL", true);
    ssd1306_ui_text(2, 48, "CATCH", true);
    ssd1306_show();
}

static void show_gentle_measure(void) {
    if (!oled_ok) return;
    ui_begin("CATCH");
    ssd1306_ui_text(2, 36, "MEASURE", true);
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
             score == GENTLE_SCORE_FIRM ? "FIRM" : "HARD");
    if (gentle_saturated) {
        ssd1306_ui_text(2, 36, "PEAK", true);
        ssd1306_ui_text_scaled(gentle_has_highg ? 15 : 21, 48, gentle_has_highg ? "320" : "16", true, 2);
        ssd1306_ui_text_scaled(21, 70, "G+", true, 2);
    } else {
        if (gentle_peak_axis_counts >= 100u*ACCEL_COUNTS_PER_G)
            snprintf(peak, sizeof peak, "%lu", (unsigned long)(gentle_peak_axis_counts / ACCEL_COUNTS_PER_G));
        else snprintf(peak, sizeof peak, "%lu.%lu",
                 (unsigned long)(gentle_peak_axis_counts / ACCEL_COUNTS_PER_G),
                 (unsigned long)((gentle_peak_axis_counts % ACCEL_COUNTS_PER_G) * 10u / ACCEL_COUNTS_PER_G));
        ssd1306_ui_text(2, 36, "PEAK", true);
        // Center the large peak in the full-width canvas; units use a separate line.
        ssd1306_ui_text_scaled((uint8_t)((64u-(strlen(peak)*12u-2u))/2u), 48, peak, true, 2);
        ssd1306_ui_text_scaled(27, 70, "G", true, 2);
    }
    ssd1306_ui_text(2, 112, "MID EXIT", true);
    ssd1306_show();
}

static void show_hammer(void) {
    if (!oled_ok) return;
    char line[16];
    ui_begin("HAMMER HIT");
    if (capture_complete) {
        bool data_error=capture_fifo_overrun || capture_dual_failed || !trigger_detected;
        ssd1306_ui_text(2,30,"RECORDING",true);
        ssd1306_ui_text(2,44,data_error ? "DATA ERROR" : "COMPLETE",true);
        if (capture_count) {
            snprintf(line,sizeof line,"MAX %lu.%lug",(unsigned long)(hammer_peak_mg/1000u),
                     (unsigned long)((hammer_peak_mg%1000u)/100u));
            ssd1306_ui_text(2,64,line,true);
        } else ssd1306_ui_text(2,64,"NO DATA",true);
        ssd1306_ui_text(2,78,"OVER 20g",true);
        if (capture_has_highg && capture_count && !hammer_limit_unknown && !data_error) {
            uint32_t tenths=(hammer_over20_samples*10000u+capture_rate/2u)/capture_rate;
            snprintf(line,sizeof line,"%lu.%lums",(unsigned long)(tenths/10u),
                     (unsigned long)(tenths%10u));
        } else snprintf(line,sizeof line,"N/A");
        ssd1306_ui_text(2,90,line,true);
        ssd1306_ui_text(2,102,hammer_limit_unknown ? "CLIPPED" : data_error ? "CHECK DATA" :
                       !capture_has_highg ? "NEEDS V2" : "",true);
        ssd1306_ui_text(2,116,"MID OPT",true);
    } else if (!capture_active) {
        ssd1306_ui_text(2,34,"TURN BACK",true);
        ssd1306_ui_text(2,50,"THEN PAUSE",true);
        ssd1306_ui_text(2,66,"ANY AXIS",true);
        ssd1306_ui_text(2,82,"MID BEGIN",true);
        ssd1306_ui_text(2,112,"UP HOME",true);
    } else if (hammer.phase == HAMMER_RECORDING) {
        ssd1306_ui_text(2,36,"RECORDING",true);
        ssd1306_ui_text(2,54,"5 SEC",true);
    } else if (hammer.phase >= HAMMER_ARMED) {
        ssd1306_ui_text(2,34,"ARMED",true);
        uint32_t elapsed=to_ms_since_boot(get_absolute_time())-hammer_armed_ms;
        snprintf(line,sizeof line,"%lu SEC",(unsigned long)(elapsed<120000u ? (120000u-elapsed+999u)/1000u : 0u));
        ssd1306_ui_text(2,54,line,true);
        ssd1306_ui_text(2,112,"MID EXIT",true);
    } else {
        snprintf(line,sizeof line,"TURNS %u/2",hammer.turns);
        ssd1306_ui_text(2,34,line,true);
        const char *step=hammer.phase==HAMMER_TURN ? "TURN ONCE" :
                         hammer.phase==HAMMER_REVERSE ? "TURN BACK" : "EASY PAUSE";
        ssd1306_ui_text(2,54,step,true);
        unsigned progress=hammer.phase==HAMMER_FINAL_SETTLE ? hammer.quiet_us/1000u :
            (unsigned)(hammer.phase==HAMMER_REVERSE ? hammer.reverse_degrees :
                sqrtf(hammer.turn[0]*hammer.turn[0]+hammer.turn[1]*hammer.turn[1]+hammer.turn[2]*hammer.turn[2]));
        snprintf(line,sizeof line,hammer.phase==HAMMER_FINAL_SETTLE ? "%u MS" : "%u DEG",progress);
        ssd1306_ui_text(2,72,line,true);
        ssd1306_ui_text(2,88,hammer.phase==HAMMER_FINAL_SETTLE ? "RELAX HAND" : "ANY AXIS",true);
        ssd1306_ui_text(2,112,"MID EXIT",true);
    }
    if (capture_active) ssd1306_schedule_show();
    else ssd1306_show();
}

static void show_capture_wait_still(void) {
    if (!oled_ok) return;
    ui_begin("RECORD");
    ssd1306_ui_text(2, 34, "HOLD", true);
    ssd1306_ui_text(2, 46, "STILL", true);
    ssd1306_ui_text(2, 112, "ARMING", true);
    ssd1306_show();
}

static void show_capture_armed(void) {
    sound_play(SOUND_READY);
    if (!oled_ok) return;
    ui_begin("READY");
    ssd1306_ui_text(2, 36, "WAIT", true);
    ssd1306_ui_text(2, 48, "DROP", true);
    ssd1306_show();
}

static void show_capture_event(void) {
    sound_play(SOUND_RECORDING);
    if (!oled_ok) return;
    ui_begin(trigger_was_freefall ? "FALL" : "IMPACT");
    ssd1306_ui_text(2, 36, "EVENT", true);
    ssd1306_ui_text(2, 48, "FOUND", true);
    ssd1306_ui_text(2, 112, "SAVING", true);
    ssd1306_show();
}

static void show_capture_complete(void) {
    if (capture_is_beam) { beam_state=BEAM_RESULT; show_beam(); return; }
    if (capture_is_hammer) { show_hammer(); return; }
    if (!oled_ok) return;
    ui_begin("DONE");
    ssd1306_ui_text(2, 36, "USB", true);
    ssd1306_ui_text(2, 48, "DL", true);
    ssd1306_ui_text(2, 112, "MID OPT", true);
    ssd1306_show();
}

static void show_capture_options(void) {
    if (!oled_ok) return;
    ui_begin("SAVED");
    ui_menu_item(30, "ERASE", ui_selection == 0);
    ui_menu_item(43, "BACK", ui_selection == 1);
    ssd1306_ui_text(2, 112, "MID GO", true);
    ssd1306_show();
}

static void show_erase_confirm(void) {
    if (!oled_ok) return;
    ui_begin("ERASE");
    ui_menu_item(30, "NO", ui_selection == 0);
    ui_menu_item(43, "YES", ui_selection == 1);
    ssd1306_ui_text(2, 112, "MID GO", true);
    ssd1306_show();
}

// Cancel the RAM candidate without installing it or changing a saved recording.

void capture_app_draw(void) {
    switch (ui_state) {
    case UI_HOME: home_draw(); break;
    case UI_APP: break;
    case UI_RECORD: show_record_menu(); break;
    case UI_DROP_TEST: show_drop_test_menu(); break;
    case UI_CHALLENGES: show_challenges_menu(); break;
    case UI_BEAM: show_beam(); break;
    case UI_GENTLE_CATCH:
        if (gentle_result_available) show_gentle_result();
        else show_gentle_ready();
        break;
    case UI_HAMMER_HIT: show_hammer(); break;
    case UI_CAPTURE_COMPLETE: show_capture_complete(); break;
    case UI_CAPTURE_OPTIONS: show_capture_options(); break;
    case UI_ERASE_CONFIRM: show_erase_confirm(); break;
    }
}

static void show_ui(void) {
    if (idle_standby) return;
    if (app_current()) app_draw(); else capture_app_draw();
}

// Stage one is cooperative standby. RAM, the capture buffers, and USB stay on.
static void update_status_leds(bool imu_ok);
static bool wake_idle(void) {
    if (!idle_standby) return true;
    if (!lsm6dsv_idle_end() || (oled_ok && !ssd1306_set_power(true))) {
        printf("ERROR: standby wake failed; press a button to retry.\n");
        return false;
    }
    idle_standby = false;
    ui_set_display_available(oled_ok);
    idle_power_init(&idle_power, to_ms_since_boot(get_absolute_time()));
    level_filter_initialized = false;
    update_status_leds(lsm6dsv_model() != LSM6DSV_MODEL_UNKNOWN);
    show_ui();
    printf("Power: awake (idle timeout %lu ms).\n", (unsigned long)IDLE_TIMEOUT_MS);
    return true;
}

bool capture_app_can_idle(void) {
    return !capture_active && (ui_state != UI_BEAM ||
                              beam_state == BEAM_READY || beam_state == BEAM_RESULT);
}

static void service_idle_power(bool buttons_down) {
    if (idle_standby) return;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool busy = buttons_down || capture_active || orientation_stream || orientation_fused ||
                stdio_usb_connected() || !app_can_idle();
    if (!idle_power_due(&idle_power, now, busy)) return;
    sound_stop();
    if (!lsm6dsv_idle_start()) {
        idle_power_activity(&idle_power, now);
        printf("ERROR: sensor standby failed; staying awake.\n");
        return;
    }
    if (oled_ok && !ssd1306_set_power(false)) {
        lsm6dsv_idle_end();
        idle_power_activity(&idle_power, now);
        printf("ERROR: display standby failed; staying awake.\n");
        return;
    }
    idle_standby = true;
    idle_started_ms = to_ms_since_boot(get_absolute_time());
    update_status_leds(true);
    ui_set_display_available(false);
    printf("Power: standby; press any button to wake. Recording RAM retained.\n");
}

bool app_host_can_open(void) { return !idle_standby && !capture_active && !orientation_fused; }
void app_host_prepare(void) { ui_state=UI_APP; ui_selection=0; }
void app_host_home(void) { ui_state=UI_HOME; ui_selection=0; }

void capture_app_enter(CaptureApp app) {
    ui_selection=0;
    if (app==CAPTURE_APP_HAMMER) {
        ui_state=capture_complete && !capture_is_hammer ? UI_CAPTURE_OPTIONS : UI_HAMMER_HIT;
    } else if (app==CAPTURE_APP_RECORD) ui_state=UI_RECORD;
    else if (app==CAPTURE_APP_CATCH) ui_state=UI_CHALLENGES;
    else {
        beam_state=capture_complete && capture_is_beam ? BEAM_RESULT : BEAM_READY;
        ui_state=UI_BEAM;
    }
}
uint32_t capture_app_sample_period_ms(void) {
    return ui_state==UI_BEAM && beam_state!=BEAM_READY ? 10 : 250;
}
void capture_app_refresh_live(void) {
    if (ui_state==UI_BEAM) show_beam();
}

static void sample_power(void) {
    uint32_t total = 0;
    for (unsigned i = 0; i < BATTERY_ADC_SAMPLES; ++i) total += adc_read();
    // 3.3 V reference, 12-bit ADC, and a 2:1 divider from Out+ to ADC3.
    uint16_t measured_mv = (uint16_t)(((total / BATTERY_ADC_SAMPLES) * 6600u + 2047u) / 4095u);
    bool previous_usb_state = usb_power_present;
    unsigned previous_tenths = (power_out_millivolts+50u)/100u;
    power_out_millivolts = measured_mv;
    if (usb_power_present) usb_power_present = measured_mv >= USB_PRESENT_CLEAR_MV;
    else usb_power_present = measured_mv > USB_PRESENT_SET_MV;
    ui_set_power(power_out_millivolts, usb_power_present);
    if (oled_ok && (previous_usb_state != usb_power_present ||
        (!usb_power_present &&
         previous_tenths != (power_out_millivolts+50u)/100u))) show_ui();
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
    printf("Firmware: eggbert-integrated-h7-idle2 (2026-11-13).\n");
    printf("Power: %s; idle timeout %lu ms; stage 1, button wake.\n",
           idle_standby ? "STANDBY" : "awake", (unsigned long)IDLE_TIMEOUT_MS);
    printf("IMU: %s, WHO_AM_I=0x%02X, stored high-g: %s, OLED: %s\n",
           lsm6dsv_model_name(), lsm6dsv_device_id(), capture_has_highg ? "yes" : "no",
           oled_ok ? "OK" : "NOT FOUND");
    if (capture_has_highg) {
        lsm6dsv_dual_stats_t s = lsm6dsv_dual_fifo_active() ?
            lsm6dsv_dual_fifo_stats() : capture_dual_stats;
        printf("Dual: low=%lu high=%lu pairs=%lu startup_discard=%lu sequence_errors=%lu "
               "unexpected_tags=%lu io_errors=%lu max_fifo=%u elapsed_us=%llu integrity=%s\n",
               (unsigned long)s.low_records, (unsigned long)s.high_records,
               (unsigned long)s.pairs, (unsigned long)s.startup_discarded,
               (unsigned long)s.sequence_errors, (unsigned long)s.unexpected_tags,
               (unsigned long)s.io_errors, s.max_level,
               (unsigned long long)capture_elapsed_us, capture_dual_failed ? "FAILED" : "OK");
    }
    printf("Capture: %s, %lu/%u ring samples, FIFO overrun: %s",
           capture_state_name(),
           (unsigned long)capture_count, (unsigned)capture_target,
           capture_fifo_overrun ? "YES" : "no");
    if (trigger_detected)
        printf(", trigger: %s at ring index %lu",
               capture_is_beam ? "BEAM FLICK" : trigger_was_freefall ? "FREEFALL" : "IMPACT",
               (unsigned long)trigger_index);
    printf(", %lu Hz, %lu counts/g\n", (unsigned long)capture_rate, (unsigned long)capture_scale);
    if (capture_is_hammer) {
        printf("Hammer: phase=%u turns=%u active=%u confirmed=%u peak_mg=%lu peak_unknown=%u over20_samples=%lu\n",
               (unsigned)hammer.phase,hammer.turns,capture_active,trigger_detected,
               (unsigned long)hammer_peak_mg,hammer_limit_unknown,(unsigned long)hammer_over20_samples);
        printf("Hammer gesture: reverse_deg=%u pause_ms=%lu gyro=%s\n",
               (unsigned)hammer.reverse_degrees,(unsigned long)(hammer.quiet_us/1000u),
               hammer_gyro_running ? "on" : "off");
    }
}

static size_t capture_first_index(void) {
    return capture_oldest_index;
}

// Consumers see oldest-to-newest samples even when the physical ring has wrapped.
static const lsm6dsv_accel_sample_t *capture_sample_at(size_t chronological_index) {
    size_t physical_index = (capture_first_index() + chronological_index) % CAPTURE_MAX_SAMPLES;
    return &capture_samples[physical_index];
}

static lsm6dsv_accel_sample_t legacy_sample_at(size_t index) {
    lsm6dsv_accel_sample_t s = *capture_sample_at(index);
    // Dual captures retain raw counts; preserve the existing calibrated EGG1 export.
    if (capture_has_highg || capture_is_hammer) {
        s.x=egg_correct_low(s.x,capture_metadata.offset_live[0],capture_metadata.scale_q16[0],capture_scale);
        s.y=egg_correct_low(s.y,capture_metadata.offset_live[1],capture_metadata.scale_q16[1],capture_scale);
        s.z=egg_correct_low(s.z,capture_metadata.offset_live[2],capture_metadata.scale_q16[2],capture_scale);
    }
    return s;
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
        lsm6dsv_accel_sample_t corrected = legacy_sample_at(i);
        const lsm6dsv_accel_sample_t *sample = &corrected;
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

static void download_dual_capture(void);
static void download_capture(void) {
    if (!capture_complete) {
        printf("ERROR: capture is not complete; finish or stop it before download.\n");
        return;
    }
    if (capture_has_highg) { download_dual_capture(); return; }

    uint8_t flags = 0;
    if (trigger_detected) flags |= EGG_FLAG_TRIGGERED;
    if (trigger_was_freefall) flags |= EGG_FLAG_FREEFALL;
    if (capture_fifo_overrun) flags |= EGG_FLAG_FIFO_OVERRUN;
    if (capture_dual_failed) flags |= EGG_FLAG_INVALID;
    if (capture_is_beam) flags |= EGG_FLAG_BEAM;
    if (capture_clipped) flags |= EGG_FLAG_CLIPPED;
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
    write_u32(capture_rate);
    write_u32((uint32_t)capture_count);
    write_u32(trigger_offset);
    write_u32(post_trigger_samples);
    write_u16((uint16_t)capture_scale);
    write_u16(EGG_FILE_SAMPLE_BYTES);
    write_u32(payload_crc);

    for (size_t i = 0; i < capture_count; ++i) {
        lsm6dsv_accel_sample_t corrected = legacy_sample_at(i);
        const lsm6dsv_accel_sample_t *sample = &corrected;
        write_u16((uint16_t)sample->x);
        write_u16((uint16_t)sample->y);
        write_u16((uint16_t)sample->z);
    }
    stdio_flush();
}

// Engineering-only raw export; intentionally a different magic from website EGG1.
// Header: EGD2, version/flags/u16 size, u32 rate/count/low ug/high ug/stride/CRC.
// Payload: signed int16 low XYZ then high XYZ for each shared time slot.
static void download_dual_raw(void) {
    if (capture_active || !capture_complete || !capture_has_highg) {
        printf("ERROR: no completed dual capture.\n"); return;
    }
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < capture_count; ++i) {
        size_t physical = (capture_first_index() + i) % CAPTURE_MAX_SAMPLES;
        const lsm6dsv_accel_sample_t *s[2] = {&capture_samples[physical], &capture_high_samples[physical]};
        for (unsigned sensor = 0; sensor < 2; ++sensor) {
            uint16_t v[3] = {(uint16_t)s[sensor]->x,(uint16_t)s[sensor]->y,(uint16_t)s[sensor]->z};
            for (unsigned a = 0; a < 3; ++a) {
                crc = crc32_byte(crc,(uint8_t)v[a]); crc = crc32_byte(crc,(uint8_t)(v[a] >> 8));
            }
        }
    }
    printf("DUAL RAW %lu PAIRED SAMPLES - BINARY FOLLOWS\n", (unsigned long)capture_count);
    stdio_flush();
    write_u8('E'); write_u8('G'); write_u8('D'); write_u8('2');
    write_u8(1); write_u8((capture_fifo_overrun ? 1u : 0u) | (capture_dual_failed ? 2u : 0u));
    write_u16(32); write_u32(capture_rate); write_u32((uint32_t)capture_count);
    write_u32(488); write_u32(10417); write_u32(12); write_u32(~crc);
    for (size_t i = 0; i < capture_count; ++i) {
        size_t physical = (capture_first_index() + i) % CAPTURE_MAX_SAMPLES;
        const lsm6dsv_accel_sample_t *s[2] = {&capture_samples[physical], &capture_high_samples[physical]};
        for (unsigned sensor = 0; sensor < 2; ++sensor) {
            write_u16((uint16_t)s[sensor]->x); write_u16((uint16_t)s[sensor]->y); write_u16((uint16_t)s[sensor]->z);
        }
    }
    stdio_flush();
}

// EGG2 exports raw low/high pairs and the calibration frozen at capture start.
// Selection for a graph happens later; neither raw stream is replaced by it.
// Emit bytes explicitly so the wire format does not depend on C struct padding.
static void download_dual_capture(void) {
    if (!capture_count || capture_active) {
        printf("ERROR: no completed paired samples available.\n"); return;
    }
    egg2_metadata_t m=capture_metadata;
    m.flags=(trigger_detected ? EGG_FLAG_TRIGGERED : 0u) |
            (trigger_was_freefall ? EGG_FLAG_FREEFALL : 0u) |
            (capture_fifo_overrun ? EGG_FLAG_FIFO_OVERRUN : 0u) |
            (capture_dual_failed ? EGG_FLAG_INVALID : 0u);
    m.rate=capture_rate; m.count=(uint32_t)capture_count; m.post=post_trigger_samples;
    m.trigger=trigger_detected ? (uint32_t)((trigger_index+CAPTURE_MAX_SAMPLES-capture_first_index())%CAPTURE_MAX_SAMPLES) : 0xffffffffu;
    uint32_t crc=0xffffffffu;
    for (size_t i=0; i<capture_count; ++i) {
        size_t physical=(capture_first_index()+i)%CAPTURE_MAX_SAMPLES;
        const lsm6dsv_accel_sample_t *s[2]={&capture_samples[physical],&capture_high_samples[physical]};
        for (unsigned sensor=0; sensor<2; ++sensor) {
            int16_t values[3]={s[sensor]->x,s[sensor]->y,s[sensor]->z};
            for (unsigned a=0; a<3; ++a) {
                int32_t absolute=values[a]<0 ? -(int32_t)values[a] : values[a];
                if (!sensor && absolute>=32563) m.flags|=EGG_FLAG_CLIPPED;
                if (sensor && absolute>=30413) m.flags|=EGG_FLAG_HIGH_CLIPPED;
                crc=crc32_byte(crc,(uint8_t)values[a]);
                crc=crc32_byte(crc,(uint8_t)((uint16_t)values[a]>>8));
            }
        }
    }
    m.payload_crc=~crc;
    uint8_t header[EGG2_HEADER_BYTES]; egg2_header(header,&m);
    printf("DOWNLOAD %lu PAIRED SAMPLES - BINARY FOLLOWS\n",(unsigned long)capture_count);
    stdio_flush();
    for (size_t i=0; i<sizeof header; ++i) write_u8(header[i]);
    for (size_t i=0; i<capture_count; ++i) {
        size_t physical=(capture_first_index()+i)%CAPTURE_MAX_SAMPLES;
        const lsm6dsv_accel_sample_t *s[2]={&capture_samples[physical],&capture_high_samples[physical]};
        for (unsigned sensor=0; sensor<2; ++sensor) {
            write_u16((uint16_t)s[sensor]->x); write_u16((uint16_t)s[sensor]->y); write_u16((uint16_t)s[sensor]->z);
        }
    }
    stdio_flush();
}

static bool verify_capture_range(void) {
    uint8_t ctrl8 = lsm6dsv_read_ctrl8();
    if (ctrl8 != 0x03u) {
        lsm6dsv_fifo_stop();
        sound_play(SOUND_ERROR);
        printf("ERROR: IMU CTRL8 readback is 0x%02X; expected 0x03 (+/-16 g).\n", ctrl8);
        return false;
    }
    printf("IMU capture config verified: CTRL8=0x%02X (+/-16 g, 0.488 mg/LSB).\n", ctrl8);
    return true;
}

// Snapshot calibration and configure acquisition before arming. A completed
// recording blocks rearming until explicit erase; capture protection is separate
// from which menu is currently visible.
static void start_capture(bool hammer_mode) {
    if (capture_complete) {
        printf("Capture is protected. Erase it from the EggBert menu before rearming.\n");
        return;
    }
    capture_is_beam = false;
    capture_is_hammer = hammer_mode;
    if (hammer_mode) {
        hammer_reset(&hammer);
        hammer_peak_mg=0;
        hammer_over20_samples=0;
        hammer_limit_unknown=false;
    }
    capture_has_highg = capture_dual_failed = capture_diagnostic = capture_diagnostic_roll = false;
    capture_diagnostic_seen = 0;
    capture_dual_stats = (lsm6dsv_dual_stats_t){0};
    capture_elapsed_us = 0;
    capture_metadata=(egg2_metadata_t){0};
    for (unsigned a=0; a<3; ++a) {
        capture_metadata.offset_live[a]=calibration_valid ? calibration.offset_live_counts[a] : 0;
        capture_metadata.scale_q16[a]=calibration_valid ? calibration.scale_q16[a] : 65536u;
    }
    capture_metadata.calibration_generation=calibration_valid ? calibration.generation : 0;
    capture_clipped = false;
    capture_rate = CAPTURE_RATE_HZ;
    capture_scale = ACCEL_COUNTS_PER_G;
    capture_target = CAPTURE_MAX_SAMPLES;
    capture_pretrigger = PRE_TRIGGER_SAMPLES;
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
    if (!lsm6dsv_drop_fifo_start()) {
        sound_play(SOUND_ERROR);
        printf("ERROR: could not start IMU FIFO\n");
        return;
    }
    if (!verify_capture_range()) return;
    if (hammer_mode) {
        hammer_gyro_running=lsm6dsv_gyro_start();
        if (!hammer_gyro_running) {
            lsm6dsv_gyro_stop(); lsm6dsv_fifo_stop();
            sound_play(SOUND_ERROR);
            printf("ERROR: Hammer Hit gyro configuration failed; not armed.\n");
            return;
        }
        hammer_gyro_last_ms=to_ms_since_boot(get_absolute_time());
    }
    capture_has_highg = lsm6dsv_dual_fifo_active();
    capture_started_us = time_us_64();
    capture_last_sample_ms = to_ms_since_boot(get_absolute_time());
    capture_active = true;
    capture_state = CAPTURE_WAIT_STILL;
    next_capture_poll = make_timeout_time_ms(5);
    if (hammer_mode) {
        hammer_display_ms=to_ms_since_boot(get_absolute_time());
        sound_play(SOUND_READY); show_hammer();
        printf("HAMMER H4: turn once, turn back, then pause comfortably two seconds. Any axis. Not armed yet.\n");
    } else {
        show_capture_wait_still();
        printf("Capture armed from EggBert menu: hold still for one second, then wait for freefall or impact.\n");
    }
}

static void start_dual_test(bool rolling) {
    if (capture_active || capture_complete || ui_state != UI_HOME || orientation_fused ||
        lsm6dsv_model() != LSM6DSV_MODEL_LSM6DSV320X) {
        printf("ERROR: dualtest requires v2 at HOME, idle, and no protected capture.\n"); return;
    }
    start_capture(false);
    if (!capture_active) return;
    capture_diagnostic = true;
    capture_diagnostic_roll = rolling;
    capture_state = CAPTURE_POST_EVENT;
    printf(rolling ? "DUALTEST: six seconds rolling, then retain 0.5 s before / 4.5 s after trigger.\n" :
                     "DUALTEST: timed five-second raw recording; no throw required.\n");
}

static void beam_arm(void) {
    if (capture_complete) {
        ui_state = UI_CAPTURE_OPTIONS; ui_selection = 0; show_ui();
        printf("Capture protected: download and erase it before arming Beam.\n");
        return;
    }
    capture_is_beam = true;
    capture_is_hammer = false;
    capture_has_highg = capture_dual_failed = capture_diagnostic = false;
    capture_clipped = false;
    capture_rate = BEAM_RATE_HZ;
    capture_scale = BEAM_COUNTS_PER_G;
    capture_target = BEAM_TOTAL_SAMPLES;
    capture_pretrigger = BEAM_PRE_SAMPLES;
    capture_count = capture_write_index = capture_oldest_index = trigger_index = 0;
    post_trigger_samples = 0;
    trigger_detected = trigger_was_freefall = capture_fifo_overrun = false;
    beam_result_hz = 0;
    beam_trigger_reset(&beam_trigger);
    if (!lsm6dsv_beam_fifo_start()) {
        sound_play(SOUND_ERROR);
        printf("ERROR: Beam FIFO configuration failed.\n");
        beam_state = BEAM_READY; return;
    }
    capture_active = true;
    capture_state = CAPTURE_WAIT_STILL;
    beam_state = BEAM_WAITING;
    beam_arm_at = make_timeout_time_ms(2500);
    beam_display_ms = capture_last_sample_ms = to_ms_since_boot(get_absolute_time());
    next_capture_poll = make_timeout_time_ms(5);
    show_beam();
    printf("BEAM: 480 Hz +/-4 g, 15 seconds, 0.2 g for 5 samples; settle then flick.\n");
}

static float estimate_beam_frequency(void) {
    // Ignore the first half-second after detection, including the initial yank.
    size_t trigger_offset = (trigger_index + CAPTURE_MAX_SAMPLES-capture_first_index()) % CAPTURE_MAX_SAMPLES;
    size_t begin = trigger_offset + BEAM_RATE_HZ/2u;
    if (begin >= capture_count) return 0;
    unsigned axis = 0;
    double sums[3] = {0}, squares[3] = {0};
    for (size_t i=begin; i<capture_count; ++i) {
        const lsm6dsv_accel_sample_t *s=capture_sample_at(i);
        float v[3]={s->x,s->y,s->z};
        for (unsigned a=0; a<3; ++a) { sums[a]+=v[a]; squares[a]+=v[a]*v[a]; }
    }
    for (unsigned a=1; a<3; ++a)
        if (squares[a]-sums[a]*sums[a]/(capture_count-begin) >
            squares[axis]-sums[axis]*sums[axis]/(capture_count-begin)) axis=a;
    static float signal[BEAM_SECONDS*60u];
    size_t count=0;
    for (size_t i=begin; i+8u<=capture_count; i+=8u) {
        float sum=0;
        for (size_t j=0; j<8u; ++j) {
            const lsm6dsv_accel_sample_t *s=capture_sample_at(i+j);
            sum += axis==0 ? s->x : axis==1 ? s->y : s->z;
        }
        signal[count++]=sum/8;
    }
    return beam_frequency(signal,count);
}

static uint64_t magnitude_squared(const lsm6dsv_accel_sample_t *sample);
static uint32_t sample_peak_axis(const lsm6dsv_accel_sample_t *sample);

static void summarize_hammer(void) {
    hammer_peak_mg=0;
    hammer_over20_samples=0;
    hammer_limit_unknown=false;
    bool use_high=false;
    for (size_t i=0; i<capture_count; ++i) {
        size_t physical=(capture_first_index()+i)%CAPTURE_MAX_SAMPLES;
        lsm6dsv_accel_sample_t low=legacy_sample_at(i);
        const lsm6dsv_accel_sample_t *raw=&capture_samples[physical];
        const lsm6dsv_accel_sample_t *high=&capture_high_samples[physical];
        uint32_t raw_peak=sample_peak_axis(raw), high_peak=sample_peak_axis(high);
        if (capture_has_highg)
            use_high=accel_select_high(use_high,sample_peak_axis(&low),raw_peak,high_peak);
        uint32_t mg=use_high ? (uint32_t)(sqrtf((float)magnitude_squared(high))*10.417f+0.5f) :
                              (uint32_t)(sqrtf((float)magnitude_squared(&low))*1000.0f/2048.0f+0.5f);
        bool clipped=use_high ? high_peak>=30413u : raw_peak>=32563u;
        if (clipped) hammer_limit_unknown=true;
        if (!capture_has_highg && clipped) capture_clipped=true;
        // Whole-record duration, using V2's unclipped-range sensor directly.
        // Compare squared micro-g units before rounding: strictly greater than 20 g.
        if (capture_has_highg && magnitude_squared(high)*10417ull*10417ull >
                                 20000000ull*20000000ull)
            ++hammer_over20_samples;
        if (mg>hammer_peak_mg) hammer_peak_mg=mg;
    }
}

static void finish_capture(const char *reason) {
    if (!capture_active) return;
    if (capture_is_hammer && hammer_gyro_running) {
        lsm6dsv_gyro_stop(); hammer_gyro_running=false;
    }
    if (capture_has_highg && lsm6dsv_dual_fifo_active()) {
        capture_dual_stats = lsm6dsv_dual_fifo_stats();
        capture_elapsed_us = time_us_64() - capture_started_us;
    }
    lsm6dsv_fifo_stop();
    capture_active = false;
    capture_complete = true;
    if (capture_is_hammer) summarize_hammer();
    sound_play(capture_fifo_overrun || capture_dual_failed ? SOUND_ERROR : SOUND_COMPLETE);
    capture_state = CAPTURE_COMPLETE;
    ui_state = UI_CAPTURE_COMPLETE;
    ui_selection = 0;
    if (capture_is_beam) {
        beam_result_hz = capture_fifo_overrun ? 0 : estimate_beam_frequency();
        beam_state = BEAM_RESULT; ui_state = UI_BEAM;
        show_beam();
    } else show_capture_complete();
    printf("Capture complete (%s). ", reason);
    print_capture_status();
}

static void erase_capture(void) {
    if (capture_active) return;
    capture_count = 0;
    capture_write_index = 0;
    capture_oldest_index = 0;
    capture_complete = false;
    capture_is_hammer = false;
    capture_has_highg = capture_dual_failed = capture_diagnostic = false;
    capture_fifo_overrun = false;
    trigger_detected = false;
    trigger_was_freefall = false;
    capture_state = CAPTURE_IDLE;
    app_return_home();
    show_ui();
    printf("Capture buffer erased from EggBert menu.\n");
}

// This challenge uses the FIFO for timely samples but deliberately never
// writes the RAM capture buffer. A saved .egg file therefore remains intact.
static void start_gentle_catch(void) {
    // A saved Beam recording must retain its own rate and scale metadata.
    if (!capture_complete) { capture_is_beam = false; capture_scale = ACCEL_COUNTS_PER_G; }
    gentle_measure_samples = 0;
    gentle_peak_axis_counts = 0;
    gentle_rearm_still_samples = 0;
    gentle_freefall_samples = 0;
    gentle_saturated = false;
    gentle_result_available = false;
    if (!lsm6dsv_drop_fifo_start()) {
        sound_play(SOUND_ERROR);
        printf("ERROR: could not start IMU FIFO for Gentle Catch.\n");
        return;
    }
    if (!verify_capture_range()) return;
    gentle_has_highg = lsm6dsv_dual_fifo_active();
    gentle_use_high = false;
    capture_last_sample_ms = to_ms_since_boot(get_absolute_time());
    capture_active = true;
    capture_state = CAPTURE_GENTLE_WAIT_FALL;
    ui_state = UI_GENTLE_CATCH;
    ui_selection = 0;
    next_capture_poll = make_timeout_time_ms(5);
    sound_play(SOUND_READY);
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
    sound_play(gentle_saturated ? SOUND_ERROR : SOUND_COMPLETE);
    show_gentle_result();
    if (gentle_saturated)
        printf("Gentle Catch result: sensor exceeded its %u g axis range.\n", gentle_has_highg ? 320u : 16u);
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
    if (idle_standby) {
        uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - idle_started_ms;
        bool reminder = capture_complete && capture_count > 0 &&
                        elapsed % IDLE_RECORD_LED_PERIOD_MS < IDLE_RECORD_LED_PULSE_MS;
        leds(reminder ? 0x04u : 0u);
        return;
    }
    if (!imu_ok) { leds(0x04u); return; }
    if (capture_is_hammer && capture_active && ui_state == UI_HAMMER_HIT) {
        if (hammer.phase == HAMMER_RECORDING) leds(0x03u);
        else if (hammer.phase >= HAMMER_ARMED) leds(0x01u);
        else leds((to_ms_since_boot(get_absolute_time())/400u)%2u ? 0x02u : 0u);
        return;
    }
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

// Compare squared magnitudes with squared thresholds to avoid a square root in
// trigger checks. Widen BEFORE multiplying so three full-range int16 axes fit.
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

// Trigger decisions receive calibrated low-g samples. V2 storage deliberately
// uses the original pair instead, preserving data for later recalibration/viewing.
static void store_capture_sample(const lsm6dsv_accel_sample_t *sample) {
    capture_samples[capture_write_index] = capture_has_highg ||
        (capture_is_hammer && ui_state == UI_HAMMER_HIT) ? capture_raw_pair.low : *sample;
    if (capture_has_highg) capture_high_samples[capture_write_index] = capture_raw_pair.high;
    capture_write_index = (capture_write_index + 1u) % CAPTURE_MAX_SAMPLES;
    if (capture_count < CAPTURE_MAX_SAMPLES) ++capture_count;
    else capture_oldest_index = (capture_oldest_index + 1u) % CAPTURE_MAX_SAMPLES;
}

// At the event, keep only the desired history and the trigger sample; older
// waiting-time samples can be discarded without moving the arrays in RAM.
static void retain_pretrigger_window(void) {
    // Keep exactly the rolling half-second before the trigger plus its sample.
    // This discards the menu/arming delay without copying the large RAM buffer.
    size_t keep = capture_pretrigger + 1u;
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
    uint32_t peak_axis = gentle_current_peak;

    if (capture_state == CAPTURE_GENTLE_WAIT_FALL) {
        if (magnitude < (uint64_t)FREEFALL_MAX_COUNTS * FREEFALL_MAX_COUNTS) {
            if (++gentle_freefall_samples >= GENTLE_FREEFALL_SAMPLES) {
                capture_state = CAPTURE_GENTLE_WAIT_CATCH;
                gentle_result_available = false;
                sound_play(SOUND_RECORDING);
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
                sound_play(SOUND_READY);
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
            gentle_saturated = gentle_current_clipped;
            show_gentle_measure();
        }
        return;
    }

    if (capture_state == CAPTURE_GENTLE_MEASURE) {
        if (peak_axis > gentle_peak_axis_counts) gentle_peak_axis_counts = peak_axis;
        if (gentle_current_clipped) gentle_saturated = true;
        if (++gentle_measure_samples >= GENTLE_CATCH_SAMPLES) finish_gentle_catch();
    }
}

static void handle_hammer_event(hammer_event_t event) {
    if (event == HAMMER_TRIGGERED) {
        lsm6dsv_gyro_stop(); hammer_gyro_running=false;
        uint32_t after=hammer.seq-hammer.trigger_at;
        // Confirmation arrives after flight: retain history relative to the
        // original impact, not to the confirmation sample 200+ ms later.
        trigger_index=(capture_write_index+CAPTURE_MAX_SAMPLES-1u-after)%CAPTURE_MAX_SAMPLES;
        size_t keep=capture_pretrigger+1u+after;
        if (keep>capture_count) keep=capture_count;
        capture_oldest_index=(capture_write_index+CAPTURE_MAX_SAMPLES-keep)%CAPTURE_MAX_SAMPLES;
        capture_count=keep;
        post_trigger_samples=after;
        trigger_detected=trigger_was_freefall=true;
        capture_state=CAPTURE_POST_EVENT;
        sound_stop(); sound_play(SOUND_RECORDING);
        printf("HAMMER TRIGGER: >=200 ms flight confirmed; preserving original launch and landing.\n");
    } else if (event == HAMMER_BECAME_ARMED) {
        capture_state=CAPTURE_WAIT_EVENT;
        hammer_armed_ms=hammer_tick_ms=to_ms_since_boot(get_absolute_time());
        sound_stop(); sound_play(SOUND_HAMMER_ARMED);
        printf("HAMMER ARMED: two minutes; waiting for impact followed by >=200 ms flight.\n");
    } else if (event == HAMMER_TURN_ACCEPTED) {
        sound_play(SOUND_TOSS);
        printf("HAMMER: turn %u/2 accepted. %s\n",hammer.turns,
               hammer.turns==2 ? "Pause comfortably two seconds." : "Turn back the other way.");
    } else if (event == HAMMER_RESET || event == HAMMER_TIMED_OUT) {
        capture_state=CAPTURE_WAIT_STILL;
        // Ordinary handling/partial gestures reset quietly.
        if (event == HAMMER_TIMED_OUT) { sound_stop(); sound_play(SOUND_TIMEOUT); }
        printf("HAMMER: %s. Not armed; turn once and turn back.\n",
               event==HAMMER_TIMED_OUT ? "timeout" : "gesture expired");
    }
    // Never block the acquisition loop on I2C for every sample.
    if (event != HAMMER_NONE) show_hammer();
}

static void process_hammer_sample(const lsm6dsv_accel_sample_t *sample) {
    store_capture_sample(sample);
    if (hammer.phase == HAMMER_RECORDING) {
        if (++post_trigger_samples >= POST_TRIGGER_SAMPLES)
            finish_capture("Hammer Hit five-second window complete");
        return;
    }
    handle_hammer_event(hammer_sample(&hammer,sample->x,sample->y,sample->z));
}

static void cancel_hammer(void) {
    lsm6dsv_gyro_stop(); hammer_gyro_running=false;
    lsm6dsv_fifo_stop(); capture_active=false; capture_count=0;
    capture_state=CAPTURE_IDLE; hammer_reset(&hammer);
    sound_stop(); sound_play(SOUND_TIMEOUT);
    show_hammer();
    printf("HAMMER: cancelled; no recording retained.\n");
}

static void service_hammer(void) {
    if (!capture_active || ui_state != UI_HAMMER_HIT || !capture_is_hammer) return;
    uint32_t now=to_ms_since_boot(get_absolute_time());
    if (hammer_gyro_running) {
        int16_t gyro[3];
        if (lsm6dsv_read_gyro(&gyro[0],&gyro[1],&gyro[2])) {
            hammer_gyro_last_ms=now;
            // service_capture has already drained the FIFO: use recent support
            // data, and don't let missing acceleration masquerade as a pause.
            if (now-capture_last_sample_ms>50u) hammer.supported=false;
            handle_hammer_event(hammer_gyro_sample(&hammer,gyro[0],gyro[1],gyro[2],(uint32_t)time_us_64()));
        } else if (now-hammer_gyro_last_ms>500u) {
            cancel_hammer(); sound_play(SOUND_ERROR);
            printf("ERROR: Hammer Hit gyro stopped responding; not armed.\n");
            return;
        }
    }
    if (hammer.phase == HAMMER_ARMED || hammer.phase == HAMMER_LAUNCH) {
        if (now-hammer_armed_ms>=120000u) {
            handle_hammer_event(hammer_restart(&hammer,HAMMER_TIMED_OUT));
        } else if (now-hammer_tick_ms>=1000u) {
            hammer_tick_ms=now;
            if (sound_current_cue()<0) sound_play(SOUND_TICK);
        }
    }
    if (now-hammer_display_ms>=500u) { hammer_display_ms=now; show_hammer(); }
}

static void process_capture_sample(const lsm6dsv_accel_sample_t *sample) {
    if (capture_is_hammer && ui_state == UI_HAMMER_HIT) {
        process_hammer_sample(sample); return;
    }
    if (capture_is_beam && ui_state == UI_BEAM) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        capture_last_sample_ms = now;
        store_capture_sample(sample);
        if (sample_peak_axis(sample) >= 32700u) capture_clipped = true;
        if (capture_state == CAPTURE_WAIT_STILL || capture_state == CAPTURE_WAIT_EVENT) {
            if (time_reached(beam_arm_at)) {
                bool was_ready = beam_trigger.ready;
                bool fired = beam_trigger_sample(&beam_trigger,sample->x,sample->y,sample->z);
                if (!was_ready && beam_trigger.ready) {
                    beam_state=BEAM_LISTENING; capture_state=CAPTURE_WAIT_EVENT;
                    capture_clipped=false; show_beam();
                    printf("BEAM READY: flick beam.\n");
                }
                if (fired) {
                    trigger_detected=true;
                    trigger_index=(capture_write_index+CAPTURE_MAX_SAMPLES-1u)%CAPTURE_MAX_SAMPLES;
                    retain_pretrigger_window();
                    post_trigger_samples=0; beam_state=BEAM_RECORDING;
                    capture_state=CAPTURE_POST_EVENT; show_beam();
                    printf("BEAM TRIGGER: 0.2 g sustained for 5 samples.\n");
                }
            }
        } else if (capture_state == CAPTURE_POST_EVENT) {
            if (++post_trigger_samples >= capture_target-capture_pretrigger-1u) {
                finish_capture("beam 15-second window complete"); return;
            }
        }
        if (now-beam_display_ms>=250u) { beam_display_ms=now; show_beam(); }
        return;
    }
    if (capture_state >= CAPTURE_GENTLE_WAIT_FALL) {
        process_gentle_sample(sample);
        return;
    }
    if (capture_diagnostic) {
        store_capture_sample(sample);
        if (capture_diagnostic_roll) {
            ++capture_diagnostic_seen;
            if (capture_diagnostic_seen == CAPTURE_RATE_HZ) show_capture_armed();
            if (capture_diagnostic_seen == 6u * CAPTURE_RATE_HZ) {
                trigger_detected = true;
                trigger_index = (capture_write_index + CAPTURE_MAX_SAMPLES - 1u) % CAPTURE_MAX_SAMPLES;
                retain_pretrigger_window();
                post_trigger_samples = 0;
                show_capture_event(); // exercise the same OLED latency as a real trigger
            } else if (trigger_detected && ++post_trigger_samples == POST_TRIGGER_SAMPLES) {
                finish_capture("dual rolling pretrigger window complete");
            }
        } else if (capture_count == CAPTURE_MAX_SAMPLES) finish_capture("dual timed window complete");
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

static void capture_sensor_failure(const char *reason, bool overrun) {
    sound_play(SOUND_ERROR);
    if (ui_state == UI_GENTLE_CATCH) {
        exit_gentle_catch();
        printf("ERROR: Gentle Catch stopped: %s. No result is valid for this attempt.\n", reason);
    } else {
        capture_fifo_overrun = overrun;
        capture_dual_failed = true;
        finish_capture(reason);
    }
}

// Drain FIFO batches in the main loop, not the interrupt handler. A dual-stream
// continuity fault stops acquisition rather than inventing missing sample pairs.
static void service_capture(void) {
    if (!capture_active) return;
    if ((lsm6dsv_dual_fifo_active() || (capture_is_hammer && ui_state == UI_HAMMER_HIT)) &&
        to_ms_since_boot(get_absolute_time()) - capture_last_sample_ms > 1500u) {
        capture_sensor_failure("capture sensor timeout", false); return;
    }
    if (capture_is_beam && ui_state == UI_BEAM &&
        to_ms_since_boot(get_absolute_time())-capture_last_sample_ms>1500u) {
        lsm6dsv_fifo_stop(); capture_active=false; capture_count=0;
        capture_state=CAPTURE_IDLE; beam_state=BEAM_READY;
        show_beam(); printf("ERROR: Beam sensor timeout; arm again.\n"); return;
    }

    // The watermark IRQ is the normal service signal. A 5 ms poll is only a
    // backstop for a final fragment that never reaches the 32-sample watermark.
    if (!lsm6dsv_fifo_irq_pending() &&
        absolute_time_diff_us(get_absolute_time(), next_capture_poll) > 0)
        return;
    next_capture_poll = make_timeout_time_ms(5);

    while (capture_active) {
        if (lsm6dsv_dual_fifo_active()) {
            size_t count = lsm6dsv_dual_fifo_read(fifo_pairs, 32u);
            lsm6dsv_dual_stats_t s = lsm6dsv_dual_fifo_stats();
            if (s.overrun || s.sequence_errors || s.unexpected_tags || s.io_errors) {
                capture_sensor_failure("dual FIFO integrity", s.overrun); break;
            }
            if (!count) break;
            capture_last_sample_ms = to_ms_since_boot(get_absolute_time());
            for (size_t i = 0; i < count && capture_active; ++i) {
                capture_raw_pair = fifo_pairs[i];
                lsm6dsv_accel_sample_t corrected = fifo_pairs[i].low;
                if (ui_state == UI_GENTLE_CATCH) {
                    corrected.x=calibration_correct_axis(corrected.x,0u,ACCEL_COUNTS_PER_G);
                    corrected.y=calibration_correct_axis(corrected.y,1u,ACCEL_COUNTS_PER_G);
                    corrected.z=calibration_correct_axis(corrected.z,2u,ACCEL_COUNTS_PER_G);
                    uint32_t high_peak=sample_peak_axis(&fifo_pairs[i].high);
                    gentle_use_high=accel_select_high(gentle_use_high,sample_peak_axis(&corrected),
                                                     sample_peak_axis(&fifo_pairs[i].low),high_peak);
                    gentle_current_peak=gentle_use_high ? accel_high_peak_low_counts(high_peak) : sample_peak_axis(&corrected);
                    gentle_current_clipped=gentle_use_high ? high_peak>=30413u : sample_peak_axis(&fifo_pairs[i].low)>=32563u;
                } else calibration_apply_capture_sample(&corrected);
                process_capture_sample(&corrected);
            }
            continue;
        }
        bool overrun = false;
        size_t read = lsm6dsv_fifo_read(fifo_samples, 64u, &overrun);
        if (overrun) {
            if (ui_state == UI_GENTLE_CATCH || ui_state == UI_HAMMER_HIT) { capture_sensor_failure("FIFO overflow", true); break; }
            capture_fifo_overrun = true;
        }
        if (read == 0) break;
        capture_last_sample_ms = to_ms_since_boot(get_absolute_time());
        for (size_t i = 0; i < read && capture_active; ++i) {
            if (ui_state == UI_HAMMER_HIT) capture_raw_pair.low=fifo_samples[i];
            if (ui_state == UI_GENTLE_CATCH) {
                gentle_current_clipped=sample_peak_axis(&fifo_samples[i])>=SATURATION_NEAR_COUNTS;
                fifo_samples[i].x=calibration_correct_axis(fifo_samples[i].x,0u,ACCEL_COUNTS_PER_G);
                fifo_samples[i].y=calibration_correct_axis(fifo_samples[i].y,1u,ACCEL_COUNTS_PER_G);
                fifo_samples[i].z=calibration_correct_axis(fifo_samples[i].z,2u,ACCEL_COUNTS_PER_G);
                gentle_current_peak=sample_peak_axis(&fifo_samples[i]);
            } else calibration_apply_capture_sample(&fifo_samples[i]);
            process_capture_sample(&fifo_samples[i]);
        }
    }
}

static void stop_orientation(void) {
    orientation_stream = false;
    orientation_fused = false;
    lsm6dsv_orientation_stop();
}

// Orientation temporarily owns the IMU configuration; it is separate from the
// acceleration recording. Q2 sends w,x,y,z scaled by 1,000,000 plus live sensor
// counts. The browser reorders the quaternion to its internal x,y,z,w convention.
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
        int16_t ax, ay, az, gx, gy, gz;
        if (!lsm6dsv_read_accel(&ax, &ay, &az) || !lsm6dsv_read_gyro(&gx, &gy, &gz)) return;
        printf("Q2,%lu,%ld,%ld,%ld,%ld,%d,%d,%d,%d,%d,%d\n", (unsigned long)now,
            (long)lroundf(q.w * 1000000.0f), (long)lroundf(q.x * 1000000.0f),
            (long)lroundf(q.y * 1000000.0f), (long)lroundf(q.z * 1000000.0f), ax, ay, az, gx, gy, gz);
    }
}

static void handle_serial_command(void) {
    static char command[48];
    static size_t length;
    int character;
    while ((character = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        idle_power_activity(&idle_power, to_ms_since_boot(get_absolute_time()));
        if (!wake_idle()) { length=0; continue; }
        if (character == '\r' || character == '\n') {
            if (length == 0) continue;
            command[length] = '\0';
            if (strcmp(command, "soundtest") == 0) {
                if (capture_active || orientation_fused || ui_state != UI_HOME)
                    printf("ERROR: return to idle HOME for soundtest.\n");
                else {
                    sound_play(SOUND_COMPLETE);
                    printf("Sound test: GPIO22, 2400 Hz then 3000 Hz.\n");
                }
            } else if (strcmp(command, "hammer") == 0) {
                if (capture_active || capture_complete || orientation_fused || ui_state != UI_HOME)
                    printf("ERROR: hammer diagnostic requires idle HOME and no protected capture.\n");
                else {
                    for (size_t i=0;i<app_entry_count;++i)
                        if (!strcmp(app_entries[i].id,"hammer")) { app_open(i); start_capture(true); break; }
                }
            } else if (strcmp(command, "hammer cancel") == 0) {
                if (capture_active && ui_state == UI_HAMMER_HIT && hammer.phase != HAMMER_RECORDING)
                    cancel_hammer();
                else printf("ERROR: no waiting Hammer Hit session to cancel.\n");
            } else if (strcmp(command, "soundoff") == 0) {
                sound_stop();
                printf("Sound off.\n");
            } else if (strcmp(command, "arm") == 0 || strcmp(command, "start") == 0) {
                printf("ERROR: arm EggBert using its on-device menu.\n");
            } else if (strcmp(command, "stop") == 0) {
                printf("ERROR: capture control is on EggBert; serial stop is disabled.\n");
            } else if (strcmp(command, "status") == 0) {
                print_capture_status();
                printf("Sound: GPIO22 PWM, cue=%d (-1=silent).\n", sound_current_cue());
            } else if (strcmp(command, "dualtest") == 0) {
                start_dual_test(false);
            } else if (strcmp(command, "dualtest ring") == 0) {
                start_dual_test(true);
            } else if (strcmp(command, "download raw") == 0) {
                download_dual_raw();
            } else if (strcmp(command, "download") == 0) {
                stop_orientation();
                download_capture();
            } else if (strcmp(command, "orientation fused") == 0) {
                bool orientation_screen = ui_state == UI_HOME || ui_state == UI_CAPTURE_COMPLETE ||
                    (ui_state == UI_BEAM && beam_state == BEAM_RESULT);
                if (capture_active || !orientation_screen) {
                    printf("ERROR: return EggBert to HOME or a completed recording before orientation.\n");
                } else {
                    stop_orientation();
                    orientation_fused = lsm6dsv_orientation_start();
                    orientation_last_ms = to_ms_since_boot(get_absolute_time());
                    printf(orientation_fused ? "ORIENTATION Q2 60HZ SCALE 1000000 ACCEL 8192 GYRO 70MDPS\n" : "ERROR: fusion setup failed\n");
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
                printf("Commands: status, download, orientation fused/on/off, dualtest [ring], download raw, hammer, hammer cancel, soundtest, soundoff, help. Normal capture control uses the EggBert menu.\n");
            } else {
                printf("Unknown command: %s (type help)\n", command);
            }
            length = 0;
        } else if (character >= 32 && character < 127) {
            if (length < sizeof command - 1u) command[length++] = (char)character;
        }
    }
}

void capture_app_button(unsigned button) {
    if (capture_state == CAPTURE_GENTLE_RESULT) {
        if (button == 1) exit_gentle_catch();
        return;
    }
    if (ui_state == UI_HAMMER_HIT) {
        if (button == 0) app_return_home();
        else if (button == 1) {
            if (capture_complete) { ui_state=UI_CAPTURE_OPTIONS; ui_selection=0; }
            else start_capture(true);
        }
        return;
    }
    if (ui_state == UI_BEAM) {
        if (button == 1) {
            if (beam_state == BEAM_RESULT) { ui_state=UI_CAPTURE_OPTIONS; ui_selection=0; }
            else if (beam_state == BEAM_READY) beam_arm();
        } else if (button == 0) app_return_home();
        return;
    }
    if (button == 0 || button == 2) { ui_selection=(ui_selection+1u)%2u; return; }
    if (button != 1) return;
    switch (ui_state) {
    case UI_RECORD:
        if (ui_selection==0) { ui_state=UI_DROP_TEST; ui_selection=0; }
        else app_return_home();
        break;
    case UI_DROP_TEST:
        if (ui_selection==0) start_capture(false);
        else { ui_state=UI_RECORD; ui_selection=0; }
        break;
    case UI_CHALLENGES:
        if (ui_selection==0) start_gentle_catch(); else app_return_home();
        break;
    case UI_CAPTURE_COMPLETE: ui_state=UI_CAPTURE_OPTIONS; ui_selection=0; break;
    case UI_CAPTURE_OPTIONS:
        ui_state=ui_selection==0 ? UI_ERASE_CONFIRM : UI_CAPTURE_COMPLETE; ui_selection=0;
        break;
    case UI_ERASE_CONFIRM:
        if (ui_selection==0) { ui_state=UI_CAPTURE_OPTIONS; ui_selection=0; }
        else erase_capture();
        break;
    default: break;
    }
}

static void handle_button_press(unsigned button) {
    idle_power_activity(&idle_power, to_ms_since_boot(get_absolute_time()));
    if (idle_standby) { wake_idle(); idle_wake_buttons=true; return; }
    if (idle_wake_buttons) return; // consume the entire waking press/chord
    if (orientation_fused) { stop_orientation(); printf("ORIENTATION STOPPED: device menu\n"); }
    if (capture_active) {
        if (ui_state == UI_HAMMER_HIT && button == 1 && hammer.phase != HAMMER_RECORDING) cancel_hammer();
        else if (ui_state == UI_GENTLE_CATCH && button == 1) { sound_play(SOUND_BUTTON); exit_gentle_catch(); }
        else if (ui_state == UI_BEAM && button == 1 && beam_state != BEAM_RECORDING) {
            sound_play(SOUND_BUTTON);
            lsm6dsv_fifo_stop(); capture_active=false; capture_state=CAPTURE_IDLE;
            capture_count=0; beam_state=BEAM_READY; show_beam();
        }
        return;
    }
    sound_play(SOUND_BUTTON);
    if (ui_state==UI_HOME) home_button(button);
    else if (app_current()) app_button(button);
    else capture_app_button(button);
    if (!capture_active) show_ui();
}

// Cooperative scheduler: regularly service USB, acquisition and buttons. Avoid
// adding long delays here: hardware sampling continues while software is busy,
// and the finite FIFO can overflow. Sound progresses through timer alarms.
int main(void) {
    stdio_init_all();
    setup_gpio();
    sound_init();
    settings_init();
    sleep_ms(1500);
    printf("EggBert RP2354A bring-up starting\n");

    sound_play(SOUND_STARTUP);
    for (unsigned i = 0; i < 3; ++i) { leds(1u << i); sleep_ms(300); }
    leds(0);

    oled_ok = ssd1306_init();
    ui_set_display_available(oled_ok);
    if (!settings_apply_display()) printf("ERROR: could not apply saved display brightness.\n");
    printf("OLED at 0x%02X: %s\n", OLED_I2C_ADDRESS, oled_ok ? "OK" : "NOT FOUND");
    sample_power();
    bool imu_ok = lsm6dsv_init();
    calibration_load();
    printf("IMU: %s, SPI WHO_AM_I=0x%02X, %s; dual drop capture available on v2\n",
           lsm6dsv_model_name(), lsm6dsv_device_id(), imu_ok ? "OK" : "INIT FAILED");
    if (oled_ok) {
        ui_begin("BOOT");
        ssd1306_ui_text(2, 36, imu_ok ? "IMU OK" : "IMU BAD", true);
        ssd1306_show();
    }

    if (!imu_ok) sound_play(SOUND_ERROR);
    if (imu_ok) {
        show_ui();
        printf("Capture bring-up ready. Use the EggBert menu to arm a drop test; type help for serial diagnostics.\n");
    }

    unsigned stable_pressed = 0;
    unsigned last_raw_pressed = 0;
    absolute_time_t buttons_changed_at = get_absolute_time();
    absolute_time_t next_imu_report = make_timeout_time_ms(250);
    absolute_time_t next_power_sample = make_timeout_time_ms(500);
    idle_power_init(&idle_power, to_ms_since_boot(get_absolute_time()));
    while (true) {
        handle_serial_command();
        if (!idle_standby) {
            service_capture();
            service_hammer();
            if (oled_ok) ssd1306_service_show();
            service_orientation();
            if (!capture_active && !orientation_fused) app_tick();
            if (!capture_active && absolute_time_diff_us(get_absolute_time(), next_power_sample) <= 0) {
                sample_power();
                next_power_sample = make_timeout_time_ms(500);
            }
        } // ordinary services are suspended during standby
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
        if (!pressed) idle_wake_buttons=false;
        update_status_leds(imu_ok);
        if (!idle_standby && imu_ok && !capture_active && !orientation_fused && (!app_current() || !app_current()->owns_imu) && absolute_time_diff_us(get_absolute_time(), next_imu_report) <= 0) {
            int16_t x, y, z;
            if (lsm6dsv_read_accel(&x, &y, &z)) {
                live_accel_x = calibration_correct_axis(x, 0u, LIVE_ACCEL_COUNTS_PER_G);
                live_accel_y = calibration_correct_axis(y, 1u, LIVE_ACCEL_COUNTS_PER_G);
                live_accel_z = calibration_correct_axis(z, 2u, LIVE_ACCEL_COUNTS_PER_G);
                if (orientation_stream) printf("O,%d,%d,%d\n", live_accel_x, live_accel_y, live_accel_z);
                int16_t activity_xyz[3]={live_accel_x,live_accel_y,live_accel_z};
                idle_power_motion(&idle_power, activity_xyz, to_ms_since_boot(get_absolute_time()));
                update_level_filter();
                AppSample sample={.raw={x,y,z},.corrected={live_accel_x,live_accel_y,live_accel_z},
                    .filtered={level_accel_x,level_accel_y,level_accel_z},.time_ms=to_ms_since_boot(get_absolute_time())};
                app_publish_sample(&sample);
                printf("Accel raw: X=%d Y=%d Z=%d (0.061 mg/LSB)\n", x, y, z);
            }
            next_imu_report = make_timeout_time_ms(app_sample_period_ms());
        }
        service_idle_power(pressed != 0);
        sleep_ms(idle_standby ? 20 : capture_active || orientation_fused || (app_current() && app_current()->fast_tick) ? 1 : 10);
    }
}
