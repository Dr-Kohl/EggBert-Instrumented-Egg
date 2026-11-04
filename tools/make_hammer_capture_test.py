"""Extract actual firmware recording functions for ARM buffer/score tests.

Only hardware/UI are stubbed. Covers both board formats, long circular waits,
delayed trigger confirmation, exact pre/post lengths and high-g/clipping scores.
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
static uint32_t get_absolute_time(void) { return hammer.seq*1000u/HAMMER_HZ; }
static uint32_t to_ms_since_boot(uint32_t t) { return t; }
static void show_hammer(void) {}
static void sound_stop(void) {}
static void sound_play(sound_cue_t cue) { (void)cue; }
static void summarize_hammer(void);
static void finish_capture(const char *reason) {
    (void)reason; capture_active=false; capture_complete=true;
    capture_state=CAPTURE_COMPLETE; summarize_hammer();
}
'''.replace('static void sound_stop', 'void sound_stop').replace('static void sound_play', 'void sound_play')
functions = '\n'.join(function(name) for name in [
    'capture_first_index', 'capture_sample_at', 'legacy_sample_at',
    'magnitude_squared', 'sample_peak_axis', 'store_capture_sample',
    'summarize_hammer', 'process_hammer_sample'])
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
        capture_has_highg=version==2;
        capture_count=capture_oldest_index=capture_write_index=post_trigger_samples=0;
        for (unsigned a=0;a<3;++a) capture_metadata.scale_q16[a]=65536u;
        feed(2048,96,HAMMER_MS(600));
        for (unsigned toss=0;toss<3;++toss) {
            feed(4096,192,HAMMER_MS(30)); feed(0,0,HAMMER_MS(250));
            feed(6000,281,HAMMER_MS(30)); feed(2048,96,HAMMER_MS(650));
        }
        feed(2048,96,HAMMER_MS(2100));
        if (hammer.phase!=HAMMER_ARMED) return 20+version;
        feed(2048,96,40u*HAMMER_HZ); // wrap ring many times while armed
        feed(6000,281,1); feed(0,0,HAMMER_MS(100)); feed(2048,96,HAMMER_MS(600));
        if (trigger_detected) return 30+version;
        feed(30000,2000,HAMMER_MS(10));
        feed(0,0,HAMMER_MS(200)-1u);
        if (trigger_detected) return 40+version;
        feed(0,0,1);
        if (!trigger_detected || capture_count!=PRE_TRIGGER_SAMPLES+1u+HAMMER_MS(200) ||
            post_trigger_samples!=HAMMER_MS(200)) return 50+version;
        if ((trigger_index+CAPTURE_MAX_SAMPLES-capture_first_index())%CAPTURE_MAX_SAMPLES!=PRE_TRIGGER_SAMPLES)
            return 60+version;
        feed(0,0,HAMMER_MS(194));
        feed(32767,3000,HAMMER_MS(10));
        feed(2048,96,POST_TRIGGER_SAMPLES-post_trigger_samples-1u);
        if (!capture_active) return 70+version;
        feed(2048,96,1);
        if (!capture_complete || capture_active || capture_count!=19200u || post_trigger_samples!=17279u)
            return 80+version;
        if (capture_sample_at(0)->z!=2048 || capture_sample_at(1920)->z!=30000 ||
            capture_sample_at(1921)->z!=0 || capture_sample_at(19199)->z!=2048) return 90+version;
        if (version==2 && (hammer_peak_mg!=31251u || hammer_limit_unknown)) return 100+version;
        if (version==1 && (!hammer_limit_unknown || !capture_clipped)) return 110+version;
    }
    return 0;
}
'''
(root / 'build/hammer-capture-test.c').write_text('\n'.join([prefix, defines, ui, states, variables, stubs, functions, test]))
