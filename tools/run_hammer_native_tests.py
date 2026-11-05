"""Run H4 gesture, actual recording/lifecycle, driver, sound, and Beam tests.

Requires Python and a native Clang/GCC compiler (CC may name its full path).
No hardware is flashed. Generated harnesses and binaries stay in build/ or tmp/.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
os.chdir(root)
build = root / 'build'
build.mkdir(exist_ok=True)
(root / 'tmp').mkdir(exist_ok=True)
cc = os.environ.get('CC') or shutil.which('clang') or shutil.which('gcc')
if not cc:
    raise SystemExit('Set CC to a native Clang or GCC compiler.')
for script in ('make_hammer_capture_test.py', 'make_beam_capture_test.py'):
    subprocess.run([sys.executable, str(root / 'tools' / script)], check=True)
tests = [
    ('hammer_trigger_test', ['tests/hammer_trigger_test.c'], [], []),
    ('hammer_capture_test', ['build/hammer-capture-test.c'], [], []),
    ('imu_driver_test', ['tests/imu_driver_test.c', 'src/lsm6dsv.c'], ['tests/imu_stubs'], []),
    ('sound_test', ['tests/sound_test.c', 'src/sound.c'], ['tests/sound_stubs', 'tests/imu_stubs'], []),
    ('ssd1306_schedule_test', ['tests/ssd1306_schedule_test.c'], ['tests/imu_stubs'], []),
    ('beam_test', ['tests/beam_test.c', 'tmp/beam-capture-test.c'], [], ['-DBEAM_CAPTURE_TEST']),
]
for name, sources, includes, defines in tests:
    entry = build / f'{name}-entry.c'
    entry.write_text(f'int {name}(void); int main(void) {{ return {name}(); }}\n')
    binary = build / (name + ('.exe' if os.name == 'nt' else ''))
    # Extracted harnesses intentionally retain other activity globals.
    warnings = [] if name in ('hammer_capture_test', 'beam_test') else ['-Wall', '-Wextra', '-Werror']
    command = [cc, '-std=c11', '-O2', '-fno-math-errno', *warnings, '-Iinclude']
    for inc in includes:
        command += ['-I' + inc]
    command += [*defines, *sources, str(entry), '-o', str(binary)]
    if os.name != 'nt':
        command += ['-lm']
    subprocess.run(command, check=True)
    result = subprocess.run([str(binary)])
    if result.returncode:
        raise SystemExit(f'{name}: FAIL (check/line {result.returncode})')
    print(f'{name}: PASS', flush=True)
