"""Build a self-contained Windows folder and ZIP using the active Python env."""
import argparse
import hashlib
from importlib.metadata import distribution, version
import json
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--picotool', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    repo = source.parents[1]
    from batch_programmer import Firmware
    # Pinned to the tested H8.2 binary. A future firmware update must explicitly
    # update the pin and its version, never silently use 'latest'.
    expected = '5ce5f83964579878a0136b50fa99c863105ed9917d98715014b0bc0082f77207'
    if hashlib.sha256(args.firmware.read_bytes()).hexdigest() != expected:
        raise SystemExit('This package is pinned to the verified H8.2 ELF; checksum mismatch.')
    scratch = repo / 'build/batch-programmer'
    scratch.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean', '--windowed',
                    '--name', 'EggBertBatchProgrammer', '--distpath', str(scratch/'dist'),
                    '--workpath', str(scratch/'work'), '--specpath', str(scratch),
                    '--hidden-import', 'serial.tools.list_ports_windows',
                    '--paths', str(source), str(source/'batch_programmer.py')], check=True)
    folder = scratch/'dist/EggBertBatchProgrammer'
    shutil.copy2(args.firmware, folder/'eggbert_bringup.elf')
    manifest = {'file': 'eggbert_bringup.elf', 'version': 'H8.2', 'sha256': expected,
                'firmware_source_commit': 'e41cb909b53df6e183c264b2bc8ea90241ee794b',
                'programmer_version': '1.0'}
    (folder/'firmware.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    Firmware.read(folder)
    (folder/'SHA256SUMS.txt').write_text(expected+'  eggbert_bringup.elf\n', encoding='ascii')
    for name in ('picotool.exe', 'libwinpthread-1.dll', 'LICENSE.TXT', 'package.json'):
        target = folder/'picotool'/name
        target.parent.mkdir(exist_ok=True)
        shutil.copy2(args.picotool.parent/name, target)
    shutil.copy2(source/'WINPTHREADS-COPYING.txt', folder/'picotool/WINPTHREADS-COPYING.txt')
    shutil.copy2(source/'README.md', folder/'READ-ME-FIRST.txt')
    (folder/'Start EggBert Programmer.cmd').write_text(
        '@echo off\r\ncd /d "%~dp0"\r\nstart "" "%~dp0EggBertBatchProgrammer.exe"\r\n', encoding='ascii')
    source_out = folder/'source'; source_out.mkdir(exist_ok=True)
    for name in ('batch_programmer.py', 'windows_usb.py', 'requirements.txt', 'build_windows.py', 'check_package.py',
                 'requirements-build.txt', 'WINPTHREADS-COPYING.txt'):
        shutil.copy2(source/name, source_out/name)
    notices = folder/'licenses'; notices.mkdir(exist_ok=True)
    shutil.copy2(Path(sys.base_prefix)/'LICENSE.txt', notices/'PYTHON-LICENSE.txt')
    for package in ('pyserial', 'pyinstaller'):
        dist = distribution(package)
        for item in dist.files:
            if 'license' in item.name.lower() or 'copying' in item.name.lower():
                candidate = Path(dist.locate_file(item))
                if candidate.is_file() and '.dist-info' in str(item):
                    shutil.copy2(candidate, notices/(package+'-'+item.name))
    (folder/'THIRD-PARTY-NOTICES.txt').write_text(
        'Python https://www.python.org/ - license in licenses/ and bundled Tcl/Tk data.\n'
        'pySerial https://github.com/pyserial/pyserial - license in licenses/.\n'
        'PyInstaller https://github.com/pyinstaller/pyinstaller - license/bootloader exception in licenses/.\n'
        'Picotool from Arduino RP2040 tool distribution, 4.1.0-1aec55e directory.\n'
        'https://github.com/earlephilhower/pico-quick-toolchain\n'
        'https://github.com/raspberrypi/picotool - license in picotool/LICENSE.TXT.\n'
        'MinGW-w64 winpthreads - notices in picotool/WINPTHREADS-COPYING.txt.\n'
        'Firmware is the previously built H8.2 artifact; see firmware.json and SHA256SUMS.txt.\n', encoding='utf-8')
    versions = {name: version(name) for name in ('pyserial', 'pyinstaller', 'pyinstaller-hooks-contrib')}
    (folder/'build-info.json').write_text(json.dumps({'python':sys.version, 'packages':versions},indent=2)+'\n',encoding='utf-8')
    args.output_dir.mkdir(parents=True,exist_ok=True)
    zip_path = Path(shutil.make_archive(str(args.output_dir/'EggBert-H8.2-Windows'), 'zip', folder.parent, folder.name))
    digest = hashlib.sha256(zip_path.read_bytes()).hexdigest()
    (args.output_dir/'EggBert-H8.2-Windows.sha256').write_text(digest+'  '+zip_path.name+'\n',encoding='ascii')
    print(json.dumps({'folder':str(folder),'zip':str(zip_path),'sha256':digest,'bytes':zip_path.stat().st_size}))


if __name__ == '__main__':
    main()
