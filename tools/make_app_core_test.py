"""Extract shared capture/UI functions; native tests link real App modules.

Sensor operations, time and calibration flash writes are simulated by
tests/app_platform.c. The capture arrays and input restrictions are production
code, so app lifecycle tests can detect accidental loss of protected data.
"""
import re
from pathlib import Path

root=Path(__file__).resolve().parents[1]
source=(root/'src/main.c').read_text()


def function(name):
    match=re.search(r'^(?:static )?[^\n;{}]+\b'+name+r'\([^;{}]*\)\s*\{',source,re.M)
    if not match:
        raise ValueError(name)
    end,depth=match.end(),1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}'); end+=1
    return source[match.start():end]


globals_text=source[source.index('#define CAPTURE_RATE_HZ'):source.index('static uint32_t calibration_crc32')]
globals_text=globals_text.replace(function('calibration_current'),'')
names=re.findall(r'static void (show_\w+)\(void\)\s*\{',source)
names += ['capture_app_draw','app_host_can_open','app_host_prepare','app_host_home','capture_app_enter',
          'capture_app_sample_period_ms','capture_app_refresh_live','verify_capture_range','start_capture','beam_arm',
          'erase_capture','start_gentle_catch','exit_gentle_catch','cancel_hammer','capture_app_button','handle_button_press','capture_app_can_idle','wake_idle','service_idle_power']
# show_ui is called before its definition by some stubs; provide its prototype.
names.sort(key=lambda name:source.index(function(name)))
prefix='''#include <string.h>
#include <math.h>
#include "egg_format.h"
#include "beam_trigger.h"
#include "hammer_trigger.h"
#include "ui.h"
#include "home.h"
#include "sound.h"
#include "idle_power.h"
#include "pico/stdio_usb.h"
'''
stubs='''static void show_ui(void);
static void stop_orientation(void) { orientation_fused=false; orientation_stream=false; }
static void beam_arm(void);
'''
output=root/'build/app-core-test.inc'
output.parent.mkdir(exist_ok=True)
output.write_text('\n'.join([prefix,globals_text,stubs,*(function(n) for n in names)]))
print(output)
