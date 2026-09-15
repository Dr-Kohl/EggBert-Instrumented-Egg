#include <stdio.h>
#include <string.h>

#include "board.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "lsm6dsv.h"
#include "pico/stdlib.h"
#include "ssd1306.h"

static const uint LEDS[] = {LED_GREEN_PIN, LED_YELLOW_PIN, LED_RED_PIN};
static const uint BUTTONS[] = {BUTTON_1_PIN, BUTTON_2_PIN, BUTTON_3_PIN};

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

typedef enum {
    UI_HOME,
    UI_RECORD,
    UI_DROP_TEST,
    UI_CHALLENGES,
    UI_GENTLE_CATCH,
    UI_CAPTURE_COMPLETE,
    UI_CAPTURE_OPTIONS,
    UI_ERASE_CONFIRM,
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
static bool gentle_result_available;
static absolute_time_t gentle_result_led_until;

typedef enum {
    GENTLE_SCORE_GENTLE,
    GENTLE_SCORE_FIRM,
    GENTLE_SCORE_HARD,
} gentle_score_t;

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
    ssd1306_ui_text(19, y, text, !selected);
}

static void show_home(void) {
    if (!oled_ok) return;
    ui_begin("HOME", "I");
    ui_menu_item(25, "RECORD", ui_selection == 0);
    ui_menu_item(38, "CATCH", ui_selection == 1);
    ui_menu_item(51, "RESULTS", ui_selection == 2);
    ssd1306_ui_text(18, 112, "MID GO", true);
    ssd1306_show();
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
        ssd1306_ui_text_scaled(7, 48, "16G+", true, 2);
    } else {
        snprintf(peak, sizeof peak, "%lu.%luG",
                 (unsigned long)(gentle_peak_axis_counts / ACCEL_COUNTS_PER_G),
                 (unsigned long)((gentle_peak_axis_counts % ACCEL_COUNTS_PER_G) * 10u / ACCEL_COUNTS_PER_G));
        ssd1306_ui_text(18, 36, "PEAK", true);
        ssd1306_ui_text_scaled(2, 48, peak, true, 2);
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

static void show_ui(void) {
    switch (ui_state) {
    case UI_HOME: show_home(); break;
    case UI_RECORD: show_record_menu(); break;
    case UI_DROP_TEST: show_drop_test_menu(); break;
    case UI_CHALLENGES: show_challenges_menu(); break;
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
        for (size_t i = 0; i < read && capture_active; ++i)
            process_capture_sample(&fifo_samples[i]);
    }
}

static void handle_serial_command(void) {
    static char command[16];
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
                download_capture();
            } else if (strcmp(command, "clear") == 0) {
                printf("ERROR: erase captures using the EggBert menu.\n");
            } else if (strcmp(command, "help") == 0) {
                printf("Commands: status, download, help. Capture control uses the EggBert menu.\n");
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
    if (capture_active) {
        if (ui_state == UI_GENTLE_CATCH && button == 1) exit_gentle_catch();
        return;
    }

    if (capture_state == CAPTURE_GENTLE_RESULT) {
        if (button == 1) exit_gentle_catch();
        return;
    }

    if (button == 0) {
        unsigned count = ui_state == UI_HOME ? 3u : 2u;
        ui_selection = (ui_selection + count - 1u) % count;
        show_ui();
        return;
    }
    if (button == 2) {
        unsigned count = ui_state == UI_HOME ? 3u : 2u;
        ui_selection = (ui_selection + 1u) % count;
        show_ui();
        return;
    }
    if (button != 1) return;

    switch (ui_state) {
    case UI_HOME:
        if (ui_selection == 0) { ui_state = UI_RECORD; ui_selection = 0; }
        else if (ui_selection == 1) { ui_state = UI_CHALLENGES; ui_selection = 0; }
        else if (capture_complete) { ui_state = UI_CAPTURE_COMPLETE; ui_selection = 0; }
        else printf("No capture is stored. Choose RECORD to begin a drop test.\n");
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
        if (!capture_active && absolute_time_diff_us(get_absolute_time(), next_power_sample) <= 0) {
            sample_power();
            next_power_sample = make_timeout_time_ms(500);
        }
        unsigned pressed = 0;
        for (unsigned i = 0; i < 3; ++i)
            if (!gpio_get(BUTTONS[i])) pressed |= 1u << i;
        if (pressed != last_raw_pressed) {
            printf("Buttons: SW5=%u SW2=%u SW3=%u\n", pressed & 1u, (pressed >> 1) & 1u, (pressed >> 2) & 1u);
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
        if (imu_ok && !capture_active && absolute_time_diff_us(get_absolute_time(), next_imu_report) <= 0) {
            int16_t x, y, z;
            if (lsm6dsv_read_accel(&x, &y, &z)) {
                printf("Accel raw: X=%d Y=%d Z=%d (0.061 mg/LSB)\n", x, y, z);
            }
            next_imu_report = make_timeout_time_ms(250);
        }
        sleep_ms(capture_active ? 1 : 10);
    }
}
