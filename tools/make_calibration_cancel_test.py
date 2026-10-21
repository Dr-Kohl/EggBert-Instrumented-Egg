"""Build an ARM harness from the actual wizard functions in main.c.

The harness replaces time and flash writes, so cancellation and staging can be
exercised without writing a physical board's calibration sectors. Compile the
generated build/calibration-cancel-test.c with the same flags as sound_test,
entry calibration_cancel_test, then run tools/run_arm_test.py on the ELF.
"""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'src/main.c').read_text()


def function(name):
    match = re.search(r'static (?:void|unsigned) ' + name + r'\([^)]*\)\s*\{', source)
    if not match:
        raise ValueError(f'Missing function {name}')
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


start = source.index('typedef struct __attribute__((packed)) {')
end = source.index('static char calibration_feedback[12];', start) + len('static char calibration_feedback[12];')
globals_text = source[start:end]
order = re.search(r'static const unsigned home_menu_order\[\] = \{[^}]+\};', source)[0]
defines = '\n'.join(line for line in source.splitlines() if line.startswith('#define CALIBRATION_') or line.startswith('#define LIVE_ACCEL_COUNTS_PER_G'))
prefix = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
static int quiet_printf(const char *format, ...) { (void)format; return 0; }
#define printf quiet_printf
static int quiet_snprintf(char *dst, size_t size, const char *format, ...) { (void)format; if (size) dst[0]=0; return 0; }
#define snprintf quiet_snprintf
typedef uint64_t absolute_time_t;
static bool time_ready;
static absolute_time_t make_timeout_time_ms(unsigned ms) { return ms; }
static bool time_reached(absolute_time_t time) { (void)time; return time_ready; }
enum { UI_HOME, UI_CALIBRATION };
static unsigned ui_state, ui_selection;
'''
stub = r'''
static unsigned save_calls;
static bool save_ok = true;
static bool calibration_save(const calibration_record_t *candidate) {
    ++save_calls;
    if (!save_ok) return false;
    calibration_record_t record = *candidate;
    record.generation = calibration.generation+1;
    calibration = record;
    calibration_valid = true;
    return true;
}
'''
names = ['calibration_reset_wizard', 'calibration_begin_face', 'calibration_start_collection',
         'calibration_finish_face', 'calibration_commit_pending', 'calibration_exit',
         'calibration_process_raw', 'home_step']
test = r'''
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
static void six_faces(void) {
    calibration_reset_wizard(); ui_state=UI_CALIBRATION; time_ready=true;
    for (unsigned face=0; face<6; ++face) {
        calibration_begin_face();
        int16_t xyz[3]={0}; xyz[face/2]=(face&1) ? -16000 : 16000;
        for (unsigned i=0; i<CALIBRATION_SAMPLE_COUNT; ++i)
            calibration_process_raw(xyz[0],xyz[1],xyz[2]);
    }
}
int calibration_cancel_test(void) {
    calibration=(calibration_record_t){.generation=7,.offset_live_counts={33,-22,11},.scale_q16={65536,65536,65536}};
    calibration_valid=true;
    calibration_record_t original=calibration;
    calibration_reset_wizard(); ui_state=UI_CALIBRATION;
    calibration_exit();
    CHECK(ui_state==UI_HOME && ui_selection==8 && save_calls==0);
    CHECK(memcmp(&original,&calibration,sizeof original)==0);
    // Cancel during the set-down delay; delayed samples cannot resume collection.
    ui_state=UI_CALIBRATION; time_ready=false; calibration_begin_face();
    calibration_process_raw(16000,0,0); CHECK(calibration_armed);
    calibration_exit(); time_ready=true; calibration_process_raw(16000,0,0);
    CHECK(!calibration_armed && !calibration_collecting && save_calls==0);
    // Cancel in the middle of averaging and after a completed partial face.
    ui_state=UI_CALIBRATION; calibration_begin_face(); calibration_process_raw(16000,0,0);
    CHECK(calibration_collecting); calibration_exit();
    for (unsigned i=0; i<40; ++i) calibration_process_raw(16000,0,0);
    CHECK(!calibration_collecting && calibration_faces_seen==0 && save_calls==0);
    ui_state=UI_CALIBRATION; calibration_begin_face();
    for (unsigned i=0;i<CALIBRATION_SAMPLE_COUNT;++i) calibration_process_raw(16000,0,0);
    CHECK(calibration_faces_seen==1); calibration_exit();
    CHECK(memcmp(&original,&calibration,sizeof original)==0 && save_calls==0);
    // All six faces stage a candidate; they neither activate nor persist it.
    six_faces();
    CHECK(calibration_faces_seen==63 && calibration_ready_to_save && !calibration_complete);
    CHECK(memcmp(&original,&calibration,sizeof original)==0 && save_calls==0);
    calibration_exit(); calibration_commit_pending();
    CHECK(!calibration_ready_to_save && save_calls==0);
    CHECK(memcmp(&original,&calibration,sizeof original)==0);
    // Explicit save failure preserves the active record and allows cancellation.
    six_faces(); save_ok=false; calibration_commit_pending();
    CHECK(save_calls==1 && calibration_ready_to_save && !calibration_complete);
    CHECK(memcmp(&original,&calibration,sizeof original)==0); calibration_exit();
    // Only explicit successful save installs the staged calibration.
    six_faces(); save_ok=true; calibration_commit_pending();
    CHECK(save_calls==2 && calibration_complete && !calibration_ready_to_save);
    CHECK(calibration.generation==8 && calibration.offset_live_counts[0]==0);
    calibration_record_t saved=calibration; calibration_exit();
    CHECK(memcmp(&saved,&calibration,sizeof saved)==0);
    // Invalid later-axis span must not partially modify the active record.
    ui_state=UI_CALIBRATION; calibration_faces_seen=31;
    calibration_positive[0]=16000; calibration_negative[0]=-16000;
    calibration_positive[1]=10000; calibration_negative[1]=-10000;
    calibration_positive[2]=16000;
    calibration_sum[0]=calibration_sum[1]=0;
    calibration_sum[2]=-16000*(int64_t)CALIBRATION_SAMPLE_COUNT;
    for (unsigned i=0;i<3;++i) calibration_minimum[i]=calibration_maximum[i]=0;
    calibration_finish_face();
    CHECK(!calibration_ready_to_save && save_calls==2);
    CHECK(memcmp(&saved,&calibration,sizeof saved)==0);
    ui_selection=6; CHECK(home_step(true)==8);
    ui_selection=8; CHECK(home_step(true)==7 && home_step(false)==6);
    ui_selection=7; CHECK(home_step(true)==0 && home_step(false)==8);
    ui_selection=0; CHECK(home_step(false)==7);
    return 0;
}
'''
output = root / 'build/calibration-cancel-test.c'
output.write_text('\n'.join([prefix, defines, globals_text, order, stub,
                            *(function(name) for name in names), test]))
print(output)
