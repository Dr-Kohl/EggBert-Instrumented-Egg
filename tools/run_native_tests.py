"""Run the native app, Home, maze, audio, and rendered-screen regression suite.

Requires Python and a host C compiler (CC or gcc on PATH). No board is accessed.
Generated C harnesses and previews are written under build/.
"""
import os
from pathlib import Path
import shutil
import json
import re
import subprocess
import sys

root=Path(__file__).resolve().parents[1]
build=root/'build'
build.mkdir(exist_ok=True)
compiler=os.environ.get('CC') or shutil.which('gcc')
if not compiler:
    raise SystemExit('Install a native C compiler or set CC to its executable path')


def run(args):
    subprocess.run([str(arg) for arg in args],cwd=root,check=True)


def compile_run(name,sources,flags=(),strict=False):
    executable=build/(name+('.exe' if os.name=='nt' else ''))
    run([compiler,'-std=c11','-O2',*(['-Wall','-Wextra','-Werror'] if strict else []),
         *flags,*sources,'-o',executable,'-lm'])
    run([executable])


run([sys.executable,'tools/generate_apps.py','--manifest','apps.json','--output-dir','build/generated/apps'])
run([sys.executable,'tests/app_catalog_test.py'])
run([sys.executable,'tools/make_app_core_test.py'])
run([sys.executable,'tools/make_maze_test.py'])
run([sys.executable,'tools/make_calibration_cancel_test.py'])
run([sys.executable,'tools/make_ui_preview.py'])
compile_run('pendulum-detector-test',['tests/pendulum_detector_test.c','src/pendulum_detector.c'],['-Iinclude','-Dpendulum_detector_test=main'],strict=True)
compile_run('home-menu-test',['tests/home_menu_test.c','src/home_menu.c'],['-Iinclude'],strict=True)
compile_run('maze-test',['tests/maze_test.c','src/maze.c'],['-Iinclude'],strict=True)
compile_run('sound-test',['tests/sound_test.c','src/sound.c'],
            ['-Itests/sound_stubs','-Itests/imu_stubs','-Iinclude','-Dsound_test=main'],strict=True)
compile_run('settings-test',['tests/settings_test.c','tests/app_platform.c','src/ui.c','src/ssd1306.c'],
            ['-Itests/app_stubs','-Iinclude','-Itests'],strict=True)
common=['tests/app_platform.c','src/app.c','src/home.c','src/home_menu.c','src/ui.c',
        'src/ssd1306.c','src/maze.c','src/pendulum_detector.c','src/settings.c','build/generated/apps/app_registry.c']
# Existing fixtures inspect private states of the built-in apps. Additional
# catalog apps compile separately, just as in firmware.
fixture_sources=set(re.findall(r'"\.\./(src/apps/[^\"]+)"',(root/'tests/app_fixture.h').read_text()))
common += [entry['source'] for entry in json.loads((root/'apps.json').read_text())
           if entry['source'] not in fixture_sources]
includes=['-Itests/app_stubs','-Iinclude','-Itests']
compile_run('apps-test',['tests/apps_test.c',*common],includes)
compile_run('maze-session-test',['build/maze-session-test.c',*common],includes)
compile_run('calibration-cancel-test',['build/calibration-cancel-test.c',*common],
            [*includes,'-Dcalibration_cancel_test=main'])
compile_run('ui-preview',['build/ui-preview.c',*(s if s!='src/ssd1306.c' else 'build/ui-graphics-preview.c' for s in common)],includes)
run([sys.executable,'tools/make_ui_preview.py','--render'])
run([sys.executable,'tests/ui_render_test.py'])
print('All native regressions passed; OLED previews are in build/ui-preview/.')
