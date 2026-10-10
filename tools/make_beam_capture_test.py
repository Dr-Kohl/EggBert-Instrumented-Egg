"""Extract the actual firmware buffer/state functions into an ARM test harness.

Hardware, clock and display calls are stubbed. Tests exercise a long armed
wait that wraps the RAM ring, trigger retention, and exact recording length.
Output: tmp/beam-capture-test.c, linked with tests/beam_test.c.
"""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
source=(root/'src/main.c').read_text()
def function(name):
    start=re.search(r'^static [^\n]*\b'+name+r'\([^\n]*\) \{',source,re.M).start()
    opening=source.index('{',start)
    depth=1; end=opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}'); end+=1
    return source[start:end]
defines='\n'.join(line for line in source.splitlines() if re.match(
    r'#define (CAPTURE_|PRE_TRIGGER_|POST_TRIGGER_|ACCEL_COUNTS_|STILL_|FREEFALL_)',line))
ui=re.search(r'typedef enum \{\s*UI_HOME,.*?\} ui_state_t;',source,re.S).group()
states=re.search(r'typedef enum \{\s*CAPTURE_IDLE,.*?\} capture_state_t;',source,re.S).group()
beam=re.search(r'typedef enum \{ BEAM_READY,.*?\} beam_state_t;',source).group()
globals=source[source.index('static lsm6dsv_accel_sample_t capture_samples'):source.index('static bool oled_ok;')]
prefix='''#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "lsm6dsv.h"
#include "beam_trigger.h"
typedef uint32_t absolute_time_t;
#define printf(...) ((void)0)
'''
stubs='''
static ui_state_t ui_state=UI_BEAM;
static capture_state_t capture_state;
static beam_state_t beam_state;
static absolute_time_t beam_arm_at;
static uint32_t fake_time;
static uint32_t get_absolute_time(void) { return fake_time; }
static uint32_t to_ms_since_boot(uint32_t t) { return t; }
static bool time_reached(uint32_t t) { return fake_time>=t; }
static void show_beam(void) {}
static void show_capture_armed(void) {}
static void show_capture_event(void) {}
static void process_gentle_sample(const lsm6dsv_accel_sample_t *s) { (void)s; }
static void finish_capture(const char *reason) { (void)reason; capture_active=false; capture_complete=true; capture_state=CAPTURE_COMPLETE; }
'''
functions='\n'.join(function(name) for name in [
    'capture_first_index','capture_sample_at','magnitude_squared','magnitude_between',
    'store_capture_sample','retain_pretrigger_window','sample_peak_axis','process_capture_sample'])
test='''
int beam_capture_test(void) {
    capture_active=capture_is_beam=true;
    capture_rate=480; capture_scale=8192;
    capture_target=7200; capture_pretrigger=240;
    capture_state=CAPTURE_WAIT_STILL;
    lsm6dsv_accel_sample_t s={0,0,8192};
    for (unsigned i=0;i<20000;i++) { fake_time+=2; process_capture_sample(&s); }
    if (!beam_trigger.ready || trigger_detected) return 30;
    s.x=2000;
    for (unsigned i=0;i<5;i++) { fake_time+=2; process_capture_sample(&s); }
    if (!trigger_detected || capture_count!=241 || post_trigger_samples) return 31;
    if ((trigger_index+CAPTURE_MAX_SAMPLES-capture_first_index())%CAPTURE_MAX_SAMPLES!=240) return 32;
    for (unsigned i=0;i<6959;i++) {
        s.x=i; fake_time+=2; process_capture_sample(&s);
        if (!capture_active && i!=6958) return 33;
    }
    if (capture_active || !capture_complete || capture_count!=7200 || post_trigger_samples!=6959) return 34;
    if (capture_sample_at(0)->x!=0 || capture_sample_at(240)->x!=2000 ||
        capture_sample_at(241)->x!=0 || capture_sample_at(7199)->x!=6958) return 35;
    return 0;
}
'''
(root/'tmp/beam-capture-test.c').write_text('\n'.join([prefix,defines,ui,states,beam,globals,stubs,functions,test]))
