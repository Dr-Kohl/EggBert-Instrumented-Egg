"""EggBert one-at-a-time Windows batch programmer. Starts stopped; no import I/O."""
from __future__ import annotations
import argparse
import csv
from dataclasses import dataclass
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import shutil
import subprocess
import sys
import threading
import time

from windows_usb import pico_usb_devices


@dataclass(frozen=True)
class Firmware:
    path: Path
    version: str
    sha256: str
    identity: str

    @classmethod
    def read(cls, directory):
        directory = Path(directory).resolve()
        m = json.loads((directory / 'firmware.json').read_text(encoding='utf-8'))
        path = (directory / m['file']).resolve()
        if path.parent != directory or not path.is_file():
            raise ValueError('Firmware file must be beside firmware.json.')
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest != m['sha256']:
            raise ValueError('Firmware checksum mismatch. Extract a fresh copy of the package.')
        identity = 'eggbert-integrated-' + m['version']
        if identity.encode('ascii') not in path.read_bytes():
            raise ValueError('The firmware does not contain its expected version identity.')
        return cls(path, m['version'], digest, identity)


def firmware_identity(status):
    match = re.search(r'^Firmware:\s*(eggbert-[A-Za-z0-9_.-]+)\s*(?:\(|$)', status, re.M)
    return match.group(1) if match else None


def check_ready(status):
    if not firmware_identity(status):
        raise ValueError('Not identified as EggBert. No programming attempted.')
    capture = re.search(r'^Capture:\s*([^,]+),\s*(\d+)/\d+ ring samples', status, re.M)
    if not capture:
        raise ValueError('Capture state not understood. No programming attempted.')
    if capture.group(1).strip() != 'IDLE' or int(capture.group(2)):
        raise ValueError('Recording/activity present. Download recordings and return HOME before retrying.')


def check_result(status, firmware):
    if firmware_identity(status) != firmware.identity:
        raise ValueError('Flash finished but the reboot did not report ' + firmware.identity)
    if not re.search(r'^IMU:\s*LSM6DSV(?:320X)?,.*OLED:\s*OK\s*$', status, re.M):
        raise ValueError('Firmware version verified, but IMU/OLED check failed. Inspect this board.')


def find_picotool(package_dir):
    paths = [Path(package_dir) / 'picotool' / 'picotool.exe']
    if os.environ.get('EGGBERT_PICOTOOL'):
        paths.insert(0, Path(os.environ['EGGBERT_PICOTOOL']))
    if shutil.which('picotool'):
        paths.append(Path(shutil.which('picotool')))
    local = Path(os.environ.get('LOCALAPPDATA', Path.home() / 'AppData/Local'))
    home = Path.home()
    paths += sorted((local / 'Arduino15/packages/rp2040/tools/pqt-picotool').glob('*/picotool.exe'), reverse=True)
    paths += sorted((home / '.pico-sdk/picotool').glob('*/picotool.exe'), reverse=True)
    paths += sorted((home / '.pico-sdk/picotool').glob('*/bin/picotool.exe'), reverse=True)
    for path in paths:
        if path.is_file():
            return path.resolve()
    raise FileNotFoundError('Picotool not found. Use the complete Windows package, or set EGGBERT_PICOTOOL.')


class Hardware:
    def __init__(self, picotool, firmware, log):
        self.picotool, self.firmware, self.log = Path(picotool), firmware, log

    def devices(self):
        return pico_usb_devices()

    def ports(self):
        from serial.tools import list_ports
        return [p for p in list_ports.comports() if p.vid == 0x2e8a and p.pid == 0x0009]

    def status(self, port):
        import serial
        data = bytearray()
        with serial.Serial(port, 115200, timeout=.15, write_timeout=1) as stream:
            # A normal CDC open does not reset Pico SDK firmware. Allow DTR
            # and the first USB wake to settle before asking for status.
            time.sleep(.25)
            stream.reset_input_buffer()
            stream.write(b'\nstatus\n'); stream.flush()
            deadline = time.monotonic() + 2.5
            while time.monotonic() < deadline:
                data.extend(stream.read(4096))
                if b'Capture:' in data and data.endswith(b'\n'):
                    break
                if len(data) > 65536:
                    raise ValueError('Unexpected serial output; close other activities first.')
        return data.decode('utf-8', errors='replace').replace('\r', '')

    def command(self, args):
        # Never use a shell or leave target selection implicit.
        result = subprocess.run([str(self.picotool), *args], capture_output=True,
                                text=True, errors='replace', timeout=90,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.log(result.stdout + result.stderr)
        return result

    def flash(self, port, serial_number):
        if hashlib.sha256(self.firmware.path.read_bytes()).hexdigest() != self.firmware.sha256:
            raise RuntimeError('Firmware changed since Start. No programming attempted; extract a fresh package.')
        try:
            result = self.command(['load', '-v', '-x', '-f', str(self.firmware.path),
                                   '-t', 'elf', '--ser', serial_number])
        except subprocess.TimeoutExpired as error:
            # No automatic retry after an uncertain write.
            raise RuntimeError('Programming timed out. Unplug and inspect/retry this board.') from error
        if result.returncode:
            # Do not blindly retry a write or reset an unidentified device.
            raise RuntimeError('Picotool failed. See the log. Unplug this board before retrying.')
        text = result.stdout + result.stderr
        if not re.search(r'Verifying (?:Flash|flash).*?\bOK\b', text, re.S):
            raise RuntimeError('Picotool did not confirm flash verification. See the log.')

    def verify_reboot(self, serial_number):
        deadline = time.monotonic() + 18
        last = 'No matching serial port returned.'
        while time.monotonic() < deadline:
            devices = self.devices()
            if len(devices) > 1:
                raise RuntimeError('More than one Pico device connected during reboot. Disconnect extras.')
            ports = [p for p in self.ports() if (p.serial_number or '').upper() == serial_number]
            if len(ports) == 1:
                try:
                    status = self.status(ports[0].device)
                    self.log(status)
                    check_result(status, self.firmware)
                    return status
                except (OSError, ValueError) as error:
                    last = str(error)
            time.sleep(.4)
        raise RuntimeError('Could not confirm the reboot: ' + last)


class Session:
    """One poll runs at a time on the worker. A completed/failed attempt latches.

    Stop/start never clears the latch. Only continuous physical USB absence
    clears it; a short BOOTSEL/application transition is not an unplug.
    """
    def __init__(self, hardware, firmware, notify, record, clock=time.monotonic):
        self.hw, self.firmware, self.notify, self.record = hardware, firmware, notify, record
        self.clock = clock
        self.blocked = False
        self.absent_since = None
        self.busy = False
        self.successes = 0
        self.failures = 0
        self.last_message = None
        self.attempted_serials = set()

    def show(self, state, message):
        item = (state, message)
        if item != self.last_message:
            self.last_message = item
            self.notify(state, message, self.successes, self.failures)

    def poll(self, allow_program=True):
        devices = self.hw.devices()  # enumeration failures must never imply absence
        if self.blocked:
            if devices:
                self.absent_since = None
            elif self.absent_since is None:
                self.absent_since = self.clock()
            elif self.clock() - self.absent_since >= 1.5:
                self.blocked = False; self.absent_since = None
                self.show('waiting' if allow_program else 'stopped',
                          'Plug in the next EggBert.' if allow_program else 'Stopped.')
            return
        if not allow_program:
            return
        if not devices:
            self.show('waiting', 'Plug in one EggBert.'); return
        if len(devices) != 1:
            self.show('attention', 'Connect only one Pico/EggBert at a time.'); return
        ports = self.hw.ports()
        if len(ports) != 1:
            self.show('attention', 'Waiting for EggBert serial port. BOOTSEL-only boards need manual recovery.'); return
        port = ports[0]
        serial_number = (port.serial_number or '').upper()
        if not re.fullmatch(r'[0-9A-F]{16}', serial_number):
            self.show('attention', 'USB serial number unavailable. No programming attempted.'); return
        if devices[0].rsplit('\\', 1)[-1] != serial_number:
            self.show('attention', 'USB and serial identities disagree. No programming attempted.'); return
        if serial_number in self.attempted_serials:
            self.show('attention', 'This EggBert was already attempted this run.\n'
                      'Connect a different board, or restart the program for a deliberate retry.'); return
        # A delayed USB reboot cannot cause a second write even if Windows
        # briefly reports no device. Retry requires a new operator-run session.
        self.attempted_serials.add(serial_number)
        self.blocked = self.busy = True
        old_version = ''
        result, detail = 'FAILED', ''
        try:
            self.show('working', 'Identifying ' + serial_number + '...')
            status = self.hw.status(port.device)
            self.hw.log(status)
            check_ready(status)
            old_version = firmware_identity(status)
            # Recheck topology after serial I/O and immediately before writing.
            if self.hw.devices() != devices:
                raise RuntimeError('USB devices changed before programming. Reconnect one EggBert.')
            self.show('working', 'Programming ' + self.firmware.version + ' - keep connected.')
            self.hw.flash(port.device, serial_number)
            self.show('working', 'Checking reboot and ' + self.firmware.version + '...')
            self.hw.verify_reboot(serial_number)
            self.successes += 1
            result, detail = 'SUCCESS', 'Flash verified; firmware, IMU and OLED status confirmed.'
            self.show('success', 'SUCCESS - UNPLUG THIS EGGBERT')
        except Exception as error:
            self.failures += 1
            detail = str(error)
            self.show('error', detail + '\nUnplug this board before continuing.')
        finally:
            self.busy = False
            self.record(serial_number, port.device, old_version, result, detail)


class Logs:
    def __init__(self, firmware):
        base = Path(os.environ['EGGBERT_LOG_DIR']) if os.environ.get('EGGBERT_LOG_DIR') else (
            Path(os.environ.get('LOCALAPPDATA', Path.home())) / 'EggBert/BatchProgrammer/logs')
        base.mkdir(parents=True, exist_ok=True)
        name = datetime.now().strftime('%Y%m%d-%H%M%S-%f')
        self.text_path, self.csv_path = base / (name + '.log'), base / (name + '.csv')
        self.firmware = firmware
        self.text('Firmware: ' + firmware.identity + '\nSHA-256: ' + firmware.sha256)
        with self.csv_path.open('w', newline='', encoding='utf-8') as out:
            csv.writer(out).writerow(['time', 'usb_serial', 'port', 'previous_firmware',
                                     'target_firmware', 'firmware_sha256', 'result', 'detail'])

    def text(self, value):
        with self.text_path.open('a', encoding='utf-8') as out:
            out.write(datetime.now().isoformat(timespec='seconds') + ' ' + value + '\n')

    def record(self, serial_number, port, old_version, result, detail):
        with self.csv_path.open('a', newline='', encoding='utf-8') as out:
            csv.writer(out).writerow([datetime.now().isoformat(timespec='seconds'), serial_number,
                                     port, old_version, self.firmware.identity,
                                     self.firmware.sha256, result, detail])
        self.text(result + ' ' + serial_number + ': ' + detail)


def package_directory():
    return Path(sys.executable).resolve().parent if getattr(sys, 'frozen', False) else Path(__file__).resolve().parent


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Validate package and Picotool without accessing a board')
    parser.add_argument('--package-dir', type=Path, default=package_directory())
    args = parser.parse_args(argv)
    import tkinter as tk
    from tkinter import messagebox
    try:
        firmware = Firmware.read(args.package_dir)
        picotool = find_picotool(args.package_dir)
        probe = subprocess.run([str(picotool), 'version'], capture_output=True, text=True,
                               timeout=10, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        if probe.returncode:
            raise RuntimeError('Picotool could not start: ' + probe.stderr)
        if args.check:
            # Import the Windows serial backend without enumerating or opening
            # ports, catching a missing bundled dependency before school use.
            import serial
            import serial.tools.list_ports_windows
            print(json.dumps({'version': firmware.version, 'sha256': firmware.sha256,
                              'picotool': str(picotool), 'picotool_version': probe.stdout.strip()}))
            return 0
        logs = Logs(firmware)
    except Exception as error:
        if args.check:
            print(str(error), file=sys.stderr); return 1
        root = tk.Tk(); root.withdraw()
        messagebox.showerror('EggBert programmer setup', str(error)); root.destroy(); return 1
    root = tk.Tk(); root.title('EggBert Batch Programmer - ' + firmware.version)
    root.geometry('740x520'); root.minsize(680, 480); root.configure(bg='#f4f6f7')
    events = queue.Queue(); enabled = threading.Event(); quitting = threading.Event()
    def notify(state, text, ok, failed):
        logs.text(state.upper() + ': ' + text)
        events.put((state, text, ok, failed))
    hw = Hardware(picotool, firmware, logs.text)
    session = Session(hw, firmware, notify, logs.record)
    tk.Label(root, text='EggBert Batch Programmer', font=('Segoe UI', 23, 'bold'), bg='#f4f6f7').pack(pady=(24, 4))
    tk.Label(root, text='Programming ' + firmware.version + '  |  One EggBert at a time',
             font=('Segoe UI', 12), bg='#f4f6f7').pack()
    status = tk.Label(root, text='Ready - click Start', bg='#e2e8ed', fg='#243746',
                      font=('Segoe UI', 18, 'bold'), wraplength=650, padx=22, pady=28)
    status.pack(fill='both', expand=True, padx=28, pady=20)
    counts = tk.Label(root, text='Completed: 0     Needs attention: 0', font=('Segoe UI', 12), bg='#f4f6f7')
    counts.pack()
    tk.Label(root, text='Download recordings first. Flashing clears recording RAM.\n'
             'Close serial monitors and the capture webpage before starting.',
             font=('Segoe UI', 10), bg='#f4f6f7', fg='#4c5964').pack(pady=10)
    controls = tk.Frame(root, bg='#f4f6f7'); controls.pack(pady=(0, 10))
    def start():
        enabled.set(); start_button.config(state='disabled'); stop_button.config(state='normal')
        if not session.blocked:
            status.config(text='Waiting for one EggBert...', bg='#e2e8ed')
    def stop():
        enabled.clear(); start_button.config(state='normal'); stop_button.config(state='disabled')
        if session.busy:
            status.config(text='Stopping after this board finishes. Keep it connected.', bg='#fff0ca')
        elif not session.blocked:
            status.config(text='Stopped - click Start to resume', bg='#e2e8ed')
    start_button = tk.Button(controls, text='Start', font=('Segoe UI', 12), width=12, command=start)
    start_button.pack(side='left', padx=6)
    stop_button = tk.Button(controls, text='Stop', font=('Segoe UI', 12), width=12, command=stop, state='disabled')
    stop_button.pack(side='left', padx=6)
    tk.Button(controls, text='Open logs', font=('Segoe UI', 12), width=12,
              command=lambda: os.startfile(logs.csv_path.parent)).pack(side='left', padx=6)
    tk.Label(root, text='Green means flash verified + H8.2 reboot confirmed.',
             bg='#f4f6f7', fg='#4c5964', font=('Segoe UI', 9)).pack(pady=(0, 12))
    def worker():
        while not quitting.is_set():
            try:
                session.poll(allow_program=enabled.is_set())
            except Exception as error:
                enabled.clear()
                notify('error', 'Device detection failed: ' + str(error), session.successes, session.failures)
            quitting.wait(.3)
    thread = threading.Thread(target=worker, name='EggBert programmer', daemon=False)
    thread.start()
    colors = {'success': '#cdebd8', 'error': '#f8d6d3', 'attention': '#fff0ca',
              'working': '#dbeafa', 'waiting': '#e2e8ed', 'stopped': '#e2e8ed'}
    closing = False
    def close():
        nonlocal closing
        closing = True; enabled.clear(); quitting.set()
        start_button.config(state='disabled'); stop_button.config(state='disabled')
        if session.busy:
            status.config(text='Finishing this board before closing. Keep it connected.', bg='#fff0ca')
    def update():
        try:
            while True:
                state, text, ok, failed = events.get_nowait()
                status.config(text=text, bg=colors.get(state, '#e2e8ed'))
                counts.config(text=f'Completed: {ok}     Needs attention: {failed}')
                if not enabled.is_set() and not closing:
                    start_button.config(state='normal'); stop_button.config(state='disabled')
        except queue.Empty:
            pass
        if closing and not thread.is_alive():
            root.destroy(); return
        root.after(100, update)
    root.protocol('WM_DELETE_WINDOW', close)
    root.after(100, update); root.mainloop()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
