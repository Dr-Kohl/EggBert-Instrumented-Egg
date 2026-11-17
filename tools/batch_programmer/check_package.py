"""Extract and check the Windows ZIP without enumerating or programming boards."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('zip', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='EggBert package check ') as temp:
        base = Path(temp)
        with zipfile.ZipFile(args.zip) as archive:
            for name in archive.namelist():
                if not (base / name).resolve().is_relative_to(base):
                    raise ValueError('ZIP member outside destination')
            assert archive.testzip() is None
            archive.extractall(base)
        root = base/'EggBertBatchProgrammer'
        required = ['EggBertBatchProgrammer.exe', 'Start EggBert Programmer.cmd',
                    'firmware.json', 'eggbert_bringup.elf', 'SHA256SUMS.txt',
                    'READ-ME-FIRST.txt', 'picotool/picotool.exe', 'picotool/libwinpthread-1.dll',
                    'picotool/LICENSE.TXT', 'picotool/WINPTHREADS-COPYING.txt',
                    '_internal/_tkinter.pyd', '_internal/_tcl_data/init.tcl',
                    'source/batch_programmer.py', 'source/windows_usb.py',
                    'licenses/PYTHON-LICENSE.txt']
        for item in required:
            assert (root/item).is_file(), item
        assert list((root/'_internal').glob('python3*.dll')), 'Python runtime DLL'
        manifest = json.loads((root/'firmware.json').read_text(encoding='utf-8'))
        digest = hashlib.sha256((root/manifest['file']).read_bytes()).hexdigest()
        assert digest == manifest['sha256'] == (root/'SHA256SUMS.txt').read_text().split()[0]
        assert manifest['version'] == 'H8.2'
        # Frozen EXE must find everything beside itself, independent of cwd.
        result = subprocess.run([str(root/'EggBertBatchProgrammer.exe'), '--check'],
                                cwd=base, timeout=30, capture_output=True)
        assert result.returncode == 0, result.stderr
        tool = subprocess.run([str(root/'picotool/picotool.exe'), 'version'], cwd=base,
                              timeout=10, text=True, capture_output=True)
        assert tool.returncode == 0, tool.stderr
        print(json.dumps({'zip_bytes':args.zip.stat().st_size, 'firmware_sha256':digest,
                          'picotool_version':tool.stdout.strip(), 'package_check':'PASS',
                          'checked':'Extracted path with spaces; frozen EXE --check; serial backend; Tcl/Tk; firmware hash; bundled Picotool'}))


if __name__ == '__main__':
    main()
