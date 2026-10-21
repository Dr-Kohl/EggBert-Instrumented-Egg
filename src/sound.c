#include "sound.h"
#include "board.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

typedef struct { uint16_t hz, ms; } note_t;
// Zero frequency is a silent gap; zero duration terminates the cue.
static const note_t cues[][6] = {
    [SOUND_BUTTON]    = {{3000,12},{0,0}},
    [SOUND_READY]     = {{2400,45},{3000,65},{0,0}},
    [SOUND_RECORDING] = {{3600,25},{0,0}},
    [SOUND_COMPLETE]  = {{2400,55},{0,35},{3000,85},{0,0}},
    [SOUND_ERROR]     = {{1800,90},{0,60},{1800,90},{0,0}},
};
static alarm_id_t alarm;
static int active_cue = -1;
static unsigned note_index;
static bool initialized;

static void output(uint16_t hz) {
    unsigned slice = pwm_gpio_to_slice_num(BUZZER_PIN);
    pwm_set_enabled(slice, false);
    gpio_put(BUZZER_PIN, false);
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_SIO);
    if (!hz) return;
    pwm_config config = pwm_get_default_config();
    // 1 MHz PWM counter: ~0.1% frequency rounding at 3 kHz.
    pwm_config_set_clkdiv(&config, (float)clock_get_hz(clk_sys)/1000000.0f);
    unsigned period = (1000000u + hz/2u)/hz;
    pwm_config_set_wrap(&config, period-1u);
    pwm_init(slice, &config, false);
    pwm_set_gpio_level(BUZZER_PIN, period/2u);
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
    pwm_set_enabled(slice, true);
}

static int64_t next_note(alarm_id_t id, void *unused) {
    (void)unused;
    if (id != alarm || active_cue < 0) return 0;
    const note_t *note = &cues[active_cue][++note_index];
    output(note->hz);
    if (!note->ms) {
        alarm = 0;
        active_cue = -1;
        return 0;
    }
    return (int64_t)note->ms*1000; // schedule from now, without catch-up bursts
}

void sound_init(void) {
    gpio_init(BUZZER_PIN);
    gpio_put(BUZZER_PIN, false);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    initialized = true;
    sound_stop();
}

void sound_stop(void) {
    if (!initialized) return;
    uint32_t irq = save_and_disable_interrupts();
    if (alarm > 0) cancel_alarm(alarm);
    alarm = 0;
    active_cue = -1;
    output(0);
    restore_interrupts(irq);
}

void sound_play(sound_cue_t cue) {
    if (!initialized || (unsigned)cue > SOUND_ERROR) return;
    uint32_t irq = save_and_disable_interrupts();
    // Status cues take precedence over button clicks. No queue or heap buffer.
    if (active_cue > (int)cue) { restore_interrupts(irq); return; }
    if (alarm > 0) cancel_alarm(alarm);
    active_cue = cue;
    note_index = 0;
    output(cues[cue][0].hz);
    alarm = add_alarm_in_ms(cues[cue][0].ms, next_note, NULL, true);
    // An exhausted alarm pool must never leave the speaker running.
    if (alarm <= 0) { alarm = 0; active_cue = -1; output(0); }
    restore_interrupts(irq);
}

int sound_current_cue(void) {
    uint32_t irq = save_and_disable_interrupts();
    int cue = active_cue;
    restore_interrupts(irq);
    return cue;
}
