"""Extract actual firmware recording functions for ARM buffer/score tests.

Only hardware/UI are stubbed. Covers both board formats, long circular waits,
immediate impact triggering, exact pre/post lengths and high-g/clipping scores.
Run from the checkout; output is build/hammer-capture-test.c.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = (root / 'src/main.c').read_text()

def function(name):
    match = re.search(r'^static [^\n]*\b' + name + r'\([^\n]*\) \{', source, re.M)
    start = match.start()
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

defines = '\n'.join(line for line in source.splitlines() if re.match(
    r'#define (CAPTURE_|PRE_TRIGGER_|POST_TRIGGER_|ACCEL_COUNTS_|STILL_|FREEFALL_)', line))
ui = re.search(r'typedef enum \{\s*UI_HOME,.*?\} ui_state_t;', source, re.S).group()
states = re.search(r'typedef enum \{\s*CAPTURE_IDLE,.*?\} capture_state_t;', source, re.S).group()
variables = source[source.index('static lsm6dsv_accel_sample_t capture_samples'):source.index('static bool oled_ok;')]
prefix = '''
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <math.h>
#include "lsm6dsv.h"
#include "egg_format.h"
#include "accel_select.h"
#include "beam_trigger.h"
#include "hammer_trigger.h"
#include "sound.h"
typedef uint32_t absolute_time_t;
#define printf(...) ((void)0)
'''
stubs = '''
static ui_state_t ui_state=UI_HAMMER_HIT;
static capture_state_t capture_state;
static bool lifecycle, gyro_start_ok=true, gyro_ready=true;
static uint32_t wall_ms;
static unsigned gyro_stops, fifo_stops, last_sound;
static int16_t gyro_input[3];
static bool calibration_valid;
static struct { int32_t offset_live_counts[3]; uint32_t scale_q16[3],generation; } calibration;
static uint32_t get_absolute_time(void) { return lifecycle ? wall_ms : hammer.seq*1000u/HAMMER_HZ; }
static uint64_t time_us_64(void) { return (uint64_t)get_absolute_time()*1000u; }
static uint32_t make_timeout_time_ms(uint32_t ms) { return get_absolute_time()+ms; }
static void show_capture_wait_still(void) {}
static bool verify_capture_range(void) { return true; }
bool lsm6dsv_drop_fifo_start(void) { return true; }
bool lsm6dsv_dual_fifo_active(void) { return false; }
void lsm6dsv_fifo_stop(void) { ++fifo_stops; }
bool lsm6dsv_gyro_start(void) { return gyro_start_ok; }
bool lsm6dsv_read_gyro(int16_t *x,int16_t *y,int16_t *z) {
    *x=gyro_input[0]; *y=gyro_input[1]; *z=gyro_input[2]; return gyro_ready;
}
int sound_current_cue(void) { return -1; }
static uint32_t to_ms_since_boot(uint32_t t) { return t; }
static void show_hammer(void) {}
void lsm6dsv_gyro_stop(void) { ++gyro_stops; }
static void sound_stop(void) {}
static void sound_play(sound_cue_t cue) { last_sound=cue; }
static void summarize_hammer(void);
static void finish_capture(const char *reason) {
    (void)reason; capture_active=false; capture_complete=true;
    capture_state=CAPTURE_COMPLETE; summarize_hammer();
}
'''.replace('static void sound_stop', 'void sound_stop').replace('static void sound_play', 'void sound_play')
functions = '\n'.join(function(name) for name in [
    'capture_first_index', 'capture_sample_at', 'legacy_sample_at',
    'magnitude_squared', 'sample_peak_axis', 'store_capture_sample',
    'summarize_hammer', 'start_capture', 'handle_hammer_event', 'process_hammer_sample',
    'cancel_hammer', 'service_hammer'])
test = '''
static void feed(int16_t low, int16_t high, unsigned n) {
    lsm6dsv_accel_sample_t s={0,0,low};
    while (n-- && capture_active) {
        capture_raw_pair.low=s;
        capture_raw_pair.high=(lsm6dsv_accel_sample_t){0,0,high};
        process_hammer_sample(&s);
    }
}
int hammer_capture_test(void) {
    for (unsigned version=1;version<=2;++version) {
        hammer_reset(&hammer);
        capture_active=capture_is_hammer=true;
        capture_complete=trigger_detected=capture_clipped=false;
        trigger_was_freefall=true; // must be cleared by impact detection
        capture_has_highg=version==2;
        capture_count=capture_oldest_index=capture_write_index=post_trigger_samples=0;
        for (unsigned a=0;a<3;++a) capture_metadata.scale_q16[a]=65536u;
        uint32_t us=0;
        hammer_gyro_sample(&hammer,0,0,0,us);
        for (unsigned step=0;step<3;++step) {
            for (unsigned i=0;i<(step==2 ? 500u : 240u);++i) {
                feed(2048,96,16);
                us+=4167;
                int16_t rate=step==0 ? 5143 : step==1 ? -5143 : 0;
                handle_hammer_event(hammer_gyro_sample(&hammer,rate,0,0,us));
            }
        }
        if (hammer.phase!=HAMMER_ARMED) return 20+version;
        feed(2048,96,40u*HAMMER_HZ); // wrap ring many times while armed
        feed(6000,281,1); feed(0,0,HAMMER_MS(100)); feed(2048,96,HAMMER_MS(600));
        if (trigger_detected) return 30+version;
        feed(30000,2000,1); // a single impact starts recording, without flight
        if (!trigger_detected || trigger_was_freefall ||
            capture_count!=PRE_TRIGGER_SAMPLES+1u || post_trigger_samples!=0 ||
            last_sound!=SOUND_RECORDING || hammer_gyro_running) return 50+version;
        if ((trigger_index+CAPTURE_MAX_SAMPLES-capture_first_index())%CAPTURE_MAX_SAMPLES!=PRE_TRIGGER_SAMPLES)
            return 60+version;
        feed(30000,2000,HAMMER_MS(10)-1u);
        feed(2048,96,HAMMER_MS(194)); // supported throughout: no flight needed
        feed(32767,3000,HAMMER_MS(10));
        feed(2048,96,POST_TRIGGER_SAMPLES-post_trigger_samples-1u);
        if (!capture_active) return 70+version;
        feed(2048,96,1);
        if (!capture_complete || capture_active || capture_count!=19200u || post_trigger_samples!=17279u)
            return 80+version;
        if (capture_sample_at(0)->z!=2048 || capture_sample_at(1920)->z!=30000 ||
            capture_sample_at(1921)->z!=30000 || capture_sample_at(19199)->z!=2048) return 90+version;
        if (version==2 && (hammer_peak_mg!=31251u || hammer_limit_unknown || hammer_over20_samples!=2u*HAMMER_MS(10))) return 100+version;
        if (version==1 && (!hammer_limit_unknown || !capture_clipped)) return 110+version;
    }
    // Full-record integration, strict threshold and vector magnitude (not any axis).
    capture_has_highg=true; capture_count=5; capture_oldest_index=0;
    memset(capture_samples,0,sizeof capture_samples);
    memset(capture_high_samples,0,sizeof capture_high_samples);
    capture_high_samples[0].z=1919; // just below 20 g
    capture_high_samples[1].z=1920; // just above 20 g
    capture_high_samples[3]=(lsm6dsv_accel_sample_t){1400,1400,0}; // >20 g vector
    capture_high_samples[4].z=3000; // later bounce
    summarize_hammer();
    if (hammer_over20_samples!=3 || hammer_limit_unknown) return 130;
    capture_count=0; summarize_hammer();
    if (hammer_over20_samples || hammer_peak_mg) return 131;
    // Actual application lifecycle: preserve completed data on BEGIN, clean
    // up both sensors on startup failure/cancel, and reject missing gyro data.
    lifecycle=true; capture_complete=true; capture_count=123;
    unsigned stops=gyro_stops;
    start_capture(true);
    if (capture_count!=123 || capture_active || gyro_stops!=stops) return 120;
    capture_complete=false; gyro_start_ok=false;
    start_capture(true);
    if (capture_active || hammer_gyro_running || !fifo_stops || gyro_stops==stops) return 121;
    gyro_start_ok=true; wall_ms=0; gyro_ready=false;
    start_capture(true);
    if (!capture_active || !hammer_gyro_running) return 122;
    wall_ms=501; service_hammer();
    if (capture_active || hammer_gyro_running || capture_count) return 123;
    // Feed the actual main-loop gyro service with small hand movement during
    // rest, then confirm it reaches ARMED and handles cancellation cleanly.
    wall_ms=1000; gyro_ready=true; start_capture(true);
    for (unsigned step=0;step<3;++step) {
        for (unsigned i=0;i<(step==2 ? 510u : 250u);++i) {
            wall_ms+=4; capture_last_sample_ms=wall_ms;
            hammer_sample(&hammer,300,-300,2048);
            gyro_input[0]=step==0 ? 5143 : step==1 ? -5143 : 200;
            service_hammer();
        }
    }
    if (hammer.phase!=HAMMER_ARMED || capture_state!=CAPTURE_WAIT_EVENT || last_sound!=SOUND_HAMMER_ARMED) return 124;
    cancel_hammer();
    if (capture_active || capture_count || hammer_gyro_running) return 125;
    // Stale accel readings cannot complete the gyro's final pause.
    start_capture(true); hammer.phase=HAMMER_FINAL_SETTLE;
    for (unsigned i=0;i<600;++i) {
        wall_ms+=4; hammer.supported=true; gyro_input[0]=0;
        service_hammer();
    }
    if (hammer.phase==HAMMER_ARMED) return 126;
    cancel_hammer();
    return 0;
}
'''
(root / 'build/hammer-capture-test.c').write_text('\n'.join([prefix, defines, ui, states, variables, stubs, functions, test]))
