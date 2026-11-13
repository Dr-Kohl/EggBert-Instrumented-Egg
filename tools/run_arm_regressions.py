"""Run integration regressions without executing native Windows binaries.

Requires an ARM GCC toolchain, unicorn and pyelftools (see run_arm_test.py).
The unchanged exhaustive 1000-seed Maze algorithm suite remains in
run_native_tests.py; this suite checks its application lifecycle and controls.
No board is accessed. All generated files are under ignored build/ and tmp/.
"""
from pathlib import Path
import subprocess,sys
import os,shutil,argparse
root=Path(__file__).resolve().parents[1]
os.chdir(root)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc',default=os.environ.get('ARM_CC') or shutil.which('arm-none-eabi-gcc'))
args=parser.parse_args()
if not args.cc: parser.error('Supply --cc <arm-none-eabi-gcc> or set ARM_CC')
cc=args.cc
(root/'build').mkdir(exist_ok=True)
(root/'tmp').mkdir(exist_ok=True)
for script in ['generate_apps.py','make_app_core_test.py','make_maze_test.py','make_calibration_cancel_test.py','make_hammer_capture_test.py','make_beam_capture_test.py','make_ui_preview.py']:
 args=[sys.executable,'tools/'+script]
 if script=='generate_apps.py': args+=['--manifest','apps.json','--output-dir','build/generated/apps']
 subprocess.run(args,check=True)
common=['tests/app_platform.c','src/app.c','src/home.c','src/home_menu.c','src/ui.c','src/ssd1306.c','src/maze.c','src/pendulum_detector.c','src/idle_power.c','src/settings.c','build/generated/apps/app_registry.c']
tests=[('idle_power_test',['tests/idle_power_test.c','src/idle_power.c'],[],[]),('pendulum_detector_test',['tests/pendulum_detector_test.c','src/pendulum_detector.c'],[],[]),('hammer_trigger_test',['tests/hammer_trigger_test.c'],[],[]),('hammer_capture_test',['build/hammer-capture-test.c'],[],[]),('imu_driver_test',['tests/imu_driver_test.c','src/lsm6dsv.c'],['tests/imu_stubs'],[]),('sound_test',['tests/sound_test.c','src/sound.c'],['tests/sound_stubs','tests/imu_stubs'],[]),('ssd1306_schedule_test',['tests/ssd1306_schedule_test.c'],['tests/imu_stubs'],[]),('beam_test',['tests/beam_test.c','tmp/beam-capture-test.c'],[],['-DBEAM_CAPTURE_TEST']),('main',['tests/apps_test.c',*common],['tests/app_stubs','tests'],[])]
tests += [
 ('main',['tests/idle_session_test.c',*common],['tests/app_stubs','tests'],[]),
 ('main',['tests/home_menu_test.c','src/home_menu.c'],[],[]),
 ('main',['tests/settings_test.c','tests/app_platform.c','src/ui.c','src/ssd1306.c'],['tests/app_stubs','tests'],[]),
 ('main',['build/maze-session-test.c',*common],['tests/app_stubs','tests'],[]),
 ('calibration_cancel_test',['build/calibration-cancel-test.c',*common],['tests/app_stubs','tests'],[]),
]
for i,(entry,sources,includes,flags) in enumerate(tests):
 output='build/integration-'+str(i)+'.elf'
 command=[cc,'-mcpu=cortex-m0plus','-mthumb','-mfloat-abi=soft','-O2','-Iinclude',*['-I'+p for p in includes],*flags,'-nostartfiles','-Wl,-e,'+entry,'-Wl,-Ttext=0x10000000','--specs=nosys.specs',*sources,'tests/arm_stubs/runtime.c','-o',output,'-lm']
 result=subprocess.run(command,text=True,capture_output=True)
 if result.returncode:
  print(result.stdout+result.stderr); result.check_returncode()
 print(f'Checking {sources[0]}',flush=True)
 subprocess.run([sys.executable,'tools/run_arm_test.py',output],check=True)
