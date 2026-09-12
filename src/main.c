#include <stdio.h>
#include <string.h>

#include "board.h"
#include "hardware/gpio.h"
#include "lsm6dsv.h"
#include "pico/stdlib.h"
#include "ssd1306.h"

static const uint LEDS[] = {LED_GREEN_PIN, LED_YELLOW_PIN, LED_RED_PIN};
static const uint BUTTONS[] = {BUTTON_1_PIN, BUTTON_2_PIN, BUTTON_3_PIN};

#define CAPTURE_RATE_HZ       3840u
#define CAPTURE_SECONDS        10u
#define CAPTURE_MAX_SAMPLES   (CAPTURE_RATE_HZ * CAPTURE_SECONDS)
#define POST_TRIGGER_SAMPLES  (CAPTURE_RATE_HZ * 3u)

// At +/-16 g, the LSM6DSV sensitivity is about 0.488 mg/count.
#define ACCEL_COUNTS_PER_G     2048u
#define STILL_MIN_COUNTS       (ACCEL_COUNTS_PER_G * 8u / 10u)
#define STILL_MAX_COUNTS       (ACCEL_COUNTS_PER_G * 12u / 10u)
#define FREEFALL_MAX_COUNTS    (ACCEL_COUNTS_PER_G * 45u / 100u)
#define IMPACT_MIN_COUNTS      (ACCEL_COUNTS_PER_G * 2u)
#define STILL_REQUIRED_SAMPLES CAPTURE_RATE_HZ

#define EGG_FILE_VERSION        1u
#define EGG_FILE_HEADER_BYTES   32u
#define EGG_FILE_SAMPLE_BYTES    6u
#define EGG_FLAG_TRIGGERED      0x01u
#define EGG_FLAG_FREEFALL       0x02u
#define EGG_FLAG_FIFO_OVERRUN   0x04u

// 38,400 samples x 3 axes x 16 bits = 230,400 bytes.  This is intentionally
// RAM-only for the first capture bring-up; no flash writes occur.
static lsm6dsv_accel_sample_t capture_samples[CAPTURE_MAX_SAMPLES];
static lsm6dsv_accel_sample_t fifo_samples[64];
static size_t capture_count;
static size_t capture_write_index;
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

typedef enum {
    CAPTURE_IDLE,
    CAPTURE_WAIT_STILL,
    CAPTURE_WAIT_EVENT,
    CAPTURE_POST_EVENT,
    CAPTURE_COMPLETE,
} capture_state_t;

static capture_state_t capture_state = CAPTURE_IDLE;

static void setup_gpio(void) {
    for (unsigned i = 0; i < 3; ++i) {
        gpio_init(LEDS[i]); gpio_set_dir(LEDS[i], GPIO_OUT); gpio_put(LEDS[i], false);
        gpio_init(BUTTONS[i]); gpio_set_dir(BUTTONS[i], GPIO_IN); gpio_pull_up(BUTTONS[i]);
    }
}

static void leds(unsigned mask) {
    for (unsigned i = 0; i < 3; ++i) gpio_put(LEDS[i], (mask >> i) & 1u);
}

static void show_capture_ready(void) {
    if (!oled_ok) return;
    ssd1306_clear();
    ssd1306_text(0, "EGGBERT");
    ssd1306_text(2, "CAPTURE READY");
    ssd1306_text(4, "USB ARM");
    ssd1306_show();
}

static void show_capture_wait_still(void) {
    if (!oled_ok) return;
    ssd1306_clear();
    ssd1306_text(0, "EGGBERT");
    ssd1306_text(2, "CAPTURE");
    ssd1306_text(4, "HOLD STILL");
    ssd1306_text(6, "ARMING");
    ssd1306_show();
}

static void show_capture_armed(void) {
    if (!oled_ok) return;
    ssd1306_clear();
    ssd1306_text(0, "EGGBERT");
    ssd1306_text(2, "CAPTURE ARMED");
    ssd1306_text(4, "WAIT EVENT");
    ssd1306_text(6, "USB STOP");
    ssd1306_show();
}

static void show_capture_event(void) {
    if (!oled_ok) return;
    ssd1306_clear();
    ssd1306_text(0, "EGGBERT");
    ssd1306_text(2, trigger_was_freefall ? "FREEFALL" : "IMPACT");
    ssd1306_text(4, "EVENT FOUND");
    ssd1306_text(6, "CAPTURING");
    ssd1306_show();
}

static void show_capture_complete(void) {
    if (!oled_ok) return;
    char samples[20];
    snprintf(samples, sizeof samples, "%lu SAMPLES", (unsigned long)capture_count);
    ssd1306_clear();
    ssd1306_text(0, "EGGBERT");
    ssd1306_text(2, "CAPTURE COMPLETE");
    ssd1306_text(4, samples);
    ssd1306_text(6, capture_fifo_overrun ? "FIFO OVER" :
                 (!trigger_detected ? "NO EVENT" :
                  (trigger_was_freefall ? "EVENT FALL" : "EVENT IMPACT")));
    ssd1306_show();
}

static const char *capture_state_name(void) {
    switch (capture_state) {
    case CAPTURE_WAIT_STILL: return "ARMING - HOLD STILL";
    case CAPTURE_WAIT_EVENT: return "ARMED - WAITING FOR EVENT";
    case CAPTURE_POST_EVENT: return "EVENT - POST CAPTURE";
    case CAPTURE_COMPLETE: return "COMPLETE";
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
    return capture_count == CAPTURE_MAX_SAMPLES ? capture_write_index : 0u;
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
    capture_count = 0;
    capture_write_index = 0;
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
    printf("Capture armed: hold EggBert still for one second, then it will wait for freefall or impact.\n");
}

static void finish_capture(const char *reason) {
    if (!capture_active) return;
    lsm6dsv_fifo_stop();
    capture_active = false;
    capture_complete = true;
    capture_state = CAPTURE_COMPLETE;
    show_capture_complete();
    printf("Capture complete (%s). ", reason);
    print_capture_status();
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
}

static void process_capture_sample(const lsm6dsv_accel_sample_t *sample) {
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
        if (magnitude < (uint64_t)FREEFALL_MAX_COUNTS * FREEFALL_MAX_COUNTS ||
            magnitude > (uint64_t)IMPACT_MIN_COUNTS * IMPACT_MIN_COUNTS) {
            trigger_detected = true;
            trigger_was_freefall = magnitude < (uint64_t)FREEFALL_MAX_COUNTS * FREEFALL_MAX_COUNTS;
            trigger_index = capture_write_index == 0 ? CAPTURE_MAX_SAMPLES - 1u : capture_write_index - 1u;
            post_trigger_samples = 0;
            capture_state = CAPTURE_POST_EVENT;
            show_capture_event();
            printf("Event trigger: %s. Capturing %u post-event samples.\n",
                   trigger_was_freefall ? "FREEFALL" : "IMPACT", POST_TRIGGER_SAMPLES);
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
                if (capture_active) printf("Capture is already recording.\n");
                else start_capture();
            } else if (strcmp(command, "stop") == 0) {
                finish_capture("stopped by user");
            } else if (strcmp(command, "status") == 0) {
                print_capture_status();
            } else if (strcmp(command, "download") == 0) {
                download_capture();
            } else if (strcmp(command, "clear") == 0) {
                if (capture_active) finish_capture("cleared by user");
                capture_count = 0;
                capture_complete = false;
                capture_fifo_overrun = false;
                trigger_detected = false;
                capture_state = CAPTURE_IDLE;
                show_capture_ready();
                printf("Capture buffer cleared.\n");
            } else if (strcmp(command, "help") == 0) {
                printf("Commands: arm (or start), stop, status, download, clear, help\n");
            } else {
                printf("Unknown command: %s (type help)\n", command);
            }
            length = 0;
        } else if (character >= 32 && character < 127) {
            if (length < sizeof command - 1u) command[length++] = (char)character;
        }
    }
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
    bool imu_ok = lsm6dsv_init();
    printf("LSM6DSV SPI WHO_AM_I: %s\n", imu_ok ? "0x70 OK" : "NOT FOUND");
    if (oled_ok) {
        ssd1306_clear();
        ssd1306_text(0, "EGGBERT");
        ssd1306_text(2, imu_ok ? "IMU OK" : "IMU FAIL");
        ssd1306_show();
    }

    if (imu_ok) {
        show_capture_ready();
        printf("Capture bring-up ready. Type help, then start in the USB serial terminal.\n");
    }

    unsigned previous = 0;
    absolute_time_t next_imu_report = make_timeout_time_ms(250);
    while (true) {
        handle_serial_command();
        service_capture();
        unsigned pressed = 0;
        for (unsigned i = 0; i < 3; ++i)
            if (!gpio_get(BUTTONS[i])) pressed |= 1u << i;
        leds(pressed);
        if (pressed != previous) {
            printf("Buttons: SW5=%u SW2=%u SW3=%u\n", pressed & 1u, (pressed >> 1) & 1u, (pressed >> 2) & 1u);
            previous = pressed;
        }
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
