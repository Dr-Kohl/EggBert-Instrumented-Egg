"""Extract production OLED drawing functions into an ARM framebuffer preview.

Compile build/ui-preview.c with entry ui_preview using the project's bare-metal
test flags and -Iinclude. Run run_arm_test.py with --dump-symbol preview_frames
--dump-size 65536 --dump-output build/ui-preview.bin. Then run this script
with --render to write galleries and selected tutorial images. No board I/O.
"""
import json
import re
import struct
import sys
import zlib
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root/'src/main.c').read_text()
graphics = (root/'src/ssd1306.c').read_text()


def function(text, name):
    match = re.search(r'(?:static )?(?:void|int|float) '+name+r'\([^)]*\)\s*\{', text)
    if not match:
        raise ValueError(name)
    end, depth = match.end(), 1
    while depth:
        depth += (text[end] == '{')-(text[end] == '}')
        end += 1
    return text[match.start():end]


cases = [
    ('home', 'ui_selection=0; show_home();'),
    ('home-2', 'ui_selection=8; show_home();'),
    ('battery-v1', 'usb_power_present=false; preview_model=LSM6DSV_MODEL_LSM6DSV; ui_selection=0; show_home(); usb_power_present=true; preview_model=LSM6DSV_MODEL_LSM6DSV320X;'),
    ('accel', 'show_acceleration();'),
    ('raw', 'show_raw_xyz();'),
    ('level', 'show_inclinometer();'),
    ('gyro-zero', 'gyro_ok=true; gyro_session.phase=GYRO_ZEROING; show_gyro();'),
    ('gyro-peak', 'gyro_session.phase=GYRO_RUNNING; gyro_session.peak_mdps[0]=2000000; gyro_session.peak_mdps[1]=-1500000; gyro_session.peak_mdps[2]=500000; show_gyro();'),
    ('record-menu', 'ui_selection=0; show_record_menu();'),
    ('drop-menu', 'show_drop_test_menu();'),
    ('catch-menu', 'show_challenges_menu();'),
    ('record-arming', 'show_capture_wait_still();'),
    ('record-ready', 'show_capture_armed();'),
    ('record-event', 'trigger_was_freefall=true; show_capture_event();'),
    ('record-done', 'show_capture_complete();'),
    ('saved-options', 'show_capture_options();'),
    ('erase', 'ui_selection=1; show_erase_confirm();'),
    ('catch-ready', 'show_gentle_ready();'),
    ('catch-fall', 'show_gentle_fall();'),
    ('catch-measure', 'show_gentle_measure();'),
    ('catch-result', 'gentle_peak_axis_counts=204595; show_gentle_result();'),
    ('catch-320', 'gentle_peak_axis_counts=655360; show_gentle_result();'),
    ('catch-clipped', 'gentle_has_highg=true; gentle_saturated=true; show_gentle_result();'),
    ('calibrate', 'show_calibration();'),
    ('calibrate-delay', 'calibration_armed=true; show_calibration(); calibration_armed=false;'),
    ('calibrate-capture', 'calibration_collecting=true; show_calibration(); calibration_collecting=false;'),
    ('calibrate-ready', 'calibration_faces_seen=63; calibration_ready_to_save=true; show_calibration();'),
    ('calibrate-error', 'strcpy(calibration_feedback,"SAVE FAIL"); show_calibration(); calibration_ready_to_save=false;'),
    ('calibrate-saved', 'calibration_complete=true; show_calibration();'),
    ('friction-ready', 'friction_state=FRICTION_READY; show_friction();'),
    ('friction-wait', 'friction_state=FRICTION_ARMED; friction_waiting_to_arm=true; show_friction();'),
    ('friction-tilt', 'friction_waiting_to_arm=false; friction_last_stable_angle=34.5f; show_friction();'),
    ('friction-result', 'friction_state=FRICTION_RESULT; show_friction();'),
    ('friction-trials', 'friction_state=FRICTION_SUMMARY; friction_summary_selection=4; show_friction();'),
    ('friction-rearm', 'friction_summary_selection=5; show_friction();'),
    ('friction-exit', 'friction_summary_selection=6; show_friction();'),
    ('pendulum-ready', 'pendulum_state=PENDULUM_READY; show_pendulum();'),
    ('pendulum-wait', 'pendulum_state=PENDULUM_WAITING; show_pendulum();'),
    ('pendulum-swing', 'pendulum_state=PENDULUM_MEASURING; show_pendulum();'),
    ('pendulum-result', 'pendulum_state=PENDULUM_RESULT; show_pendulum();'),
    ('beam-ready', 'beam_state=BEAM_READY; show_beam();'),
    ('beam-wait', 'beam_state=BEAM_WAITING; show_beam();'),
    ('beam-listen', 'beam_state=BEAM_LISTENING; show_beam();'),
    ('beam-record', 'beam_state=BEAM_RECORDING; capture_target=BEAM_TOTAL_SAMPLES; capture_pretrigger=BEAM_PRE_SAMPLES; show_beam();'),
    ('beam-result', 'beam_state=BEAM_RESULT; beam_result_hz=4.81f; show_beam();'),
    ('beam-no-period', 'beam_result_hz=0; capture_clipped=true; show_beam();'),
]

glyph_array = graphics[graphics.index('static const uint8_t glyphs'):graphics.index('static int glyph_index')]

if '--render' not in sys.argv:
    prefix = r'''
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "lsm6dsv.h"
#include "egg_format.h"
#include "gyro_session.h"
#include "beam_trigger.h"
#include "sound.h"
#include "ssd1306.h"
typedef unsigned uint;
typedef uint64_t absolute_time_t;
#define OLED_WIDTH 128u
#define OLED_PAGES 8u
#define UI_WIDTH 64u
#define UI_HEIGHT 128u
static uint8_t framebuffer[1024];
uint8_t preview_frames[64][1024];
static unsigned frame_count, text_overflows;
static lsm6dsv_model_t preview_model=LSM6DSV_MODEL_LSM6DSV320X;
lsm6dsv_model_t lsm6dsv_model(void) { return preview_model; }
void sound_play(sound_cue_t cue) { (void)cue; }
void ssd1306_show(void) { memcpy(preview_frames[frame_count++],framebuffer,1024); }
// Bare-metal newlib locks: the preview is single threaded and has no I/O.
void *__lock___sfp_recursive_mutex;
void *__lock___malloc_recursive_mutex;
void __retarget_lock_init_recursive(_LOCK_T p) { (void)p; }
void __retarget_lock_close_recursive(_LOCK_T p) { (void)p; }
void __retarget_lock_acquire_recursive(_LOCK_T p) { (void)p; }
void __retarget_lock_release_recursive(_LOCK_T p) { (void)p; }
'''
    globals_text = source[source.index('#define CAPTURE_RATE_HZ'):source.index('static uint32_t calibration_crc32')]
    gfx_names = ['ssd1306_clear','glyph_index','ui_pixel_unchecked','ssd1306_ui_pixel','ssd1306_ui_fill_rect',
                 'ssd1306_ui_text','ssd1306_ui_text_double_height','ssd1306_ui_text_scaled']
    gfx_parts = []
    for name in gfx_names:
        part = function(graphics,name)
        if name.startswith('ssd1306_ui_text'):
            scale = 'scale' if name.endswith('_scaled') else '1u'
            part = part.replace('{', '{\n    if (strlen(text) && (x+(strlen(text)*6u-1u)*'+scale+'>64u || y+7u*'+('2u' if name.endswith('double_height') else scale)+'>128u)) ++text_overflows;',1)
        gfx_parts.append(part)
    helpers = ['ui_draw_status_header','ui_begin','ui_menu_item','format_acceleration','format_tilt',
               'format_friction_value','format_friction_mu','friction_average_mu']
    show_names = re.findall(r'static void (show_\w+)\(void\)\s*\{',source)
    names = helpers+show_names
    names.sort(key=lambda name: source.index(function(source,name)))
    body = '''
int ui_preview(void) {
    oled_ok=true; usb_power_present=true; power_out_millivolts=3900;
    live_accel_x=-32768; live_accel_y=16384; live_accel_z=1234;
    level_accel_x=1500; level_accel_y=-2500; level_accel_z=16000;
    friction_trial_count=5;
    for (unsigned i=0;i<5;++i) { friction_trial_angles[i]=25.5f+i; friction_trial_mu[i]=0.5f+i*0.1f; }
    pendulum_period_count=4; pendulum_period_total_ms=4000;
'''+ '\n'.join(code for _,code in cases)+f'\n    return text_overflows ? 100+text_overflows : frame_count!={len(cases)};\n}}\n'
    output = root/'build/ui-preview.c'
    output.write_text('\n'.join([prefix,globals_text,glyph_array,*gfx_parts,
                                 *(function(source,name) for name in names),body]))
    print(output)
else:
    frames = (root/'build/ui-preview.bin').read_bytes()
    output = root.parent/'output/eggbert/full-width-ui'
    output.mkdir(parents=True,exist_ok=True)
    def png(path,width,height,pixels):
        def chunk(kind,data):
            return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
        rows=b''.join(b'\0'+pixels[y*width:(y+1)*width] for y in range(height))
        path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))
    logical=[]
    for i,(name,_) in enumerate(cases):
        frame=frames[i*1024:(i+1)*1024]
        image=bytes(255 if frame[(x//8)*128+127-y]&(1<<(x%8)) else 0 for y in range(128) for x in range(64))
        logical.append(image)
        expanded=bytes(image[(y//3)*64+x//3] for y in range(384) for x in range(192))
        png(output/(name+'.png'),192,384,expanded)
    # Galleries keep each actual framebuffer visible at 2x, with no resampling.
    for start in range(0,len(cases),12):
        width,height=4*136,3*276
        pixels=bytearray([35])*(width*height)
        for n,image in enumerate(logical[start:start+12]):
            left=(n%4)*136+4; top=(n//4)*276+10
            for y in range(256):
                for x in range(128):pixels[(top+y)*width+left+x]=image[(y//2)*64+x//2]
        png(output/f'gallery-{start//12+1}.png',width,height,pixels)
    (output/'screens.json').write_text(json.dumps([name for name,_ in cases],indent=2)+'\n')
    print(output)
