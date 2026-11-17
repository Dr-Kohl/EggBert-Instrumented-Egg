"""No connected board is touched. Run with Python's unittest runner."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/batch_programmer'))
from batch_programmer import Firmware, Hardware, Session, check_ready, check_result, firmware_identity

SERIAL = '0123456789ABCDEF'
DEVICE = 'USB\\VID_2E8A&PID_0009\\' + SERIAL
OLD = ('Firmware: eggbert-integrated-H8.1 (2026-11-13).\n'
       'IMU: LSM6DSV320X, WHO_AM_I=0x73, stored high-g: no, OLED: OK\n'
       'Capture: IDLE, 0/19200 ring samples, FIFO overrun: no, 3840 Hz, 2048 counts/g\n')
NEW = OLD.replace('H8.1', 'H8.2')
FW = Firmware(Path('firmware.elf'), 'H8.2', 'a' * 64, 'eggbert-integrated-H8.2')


class FakeHardware:
    def __init__(self):
        self.connected = [DEVICE]
        self.port_list = [SimpleNamespace(device='COM5', serial_number=SERIAL)]
        self.before = OLD
        self.loads = []
        self.fail = False
        self.checked = []

    def devices(self): return list(self.connected)
    def ports(self): return self.port_list
    def status(self, port): return self.before
    def log(self, value): pass
    def flash(self, port, serial_number):
        self.loads.append((port, serial_number))
        if self.fail: raise RuntimeError('simulated flash failure')
    def verify_reboot(self, serial_number):
        self.checked.append(serial_number)
        check_result(NEW, FW)


class BatchTests(unittest.TestCase):
    def setUp(self):
        self.hw = FakeHardware(); self.now = 0; self.events = []; self.records = []
        self.session = Session(self.hw, FW, lambda *v: self.events.append(v),
                               lambda *v: self.records.append(v), lambda: self.now)

    def unplug(self):
        self.hw.connected = []; self.session.poll(); self.now += 1.6; self.session.poll()

    def test_success_once_until_physical_unplug(self):
        self.session.poll()
        for _ in range(20): self.session.poll()
        self.assertEqual(len(self.hw.loads), 1)
        self.assertEqual(self.session.successes, 1)
        self.assertEqual(self.records[0][-2], 'SUCCESS')
        self.assertEqual(self.hw.checked, [SERIAL])
        self.unplug(); self.hw.connected = [DEVICE]; self.session.poll()
        self.assertEqual(len(self.hw.loads), 1)  # even a real reconnect is deduplicated
        self.hw.connected = ['USB\\VID_2E8A&PID_0009\\FEDCBA9876543210']
        self.hw.port_list[0].serial_number = 'FEDCBA9876543210'
        self.session.poll()
        self.assertEqual(len(self.hw.loads), 2)

    def test_bootsel_transition_and_short_gap_do_not_rearm(self):
        self.session.poll()
        self.hw.connected = []; self.session.poll(); self.now += 1
        self.hw.connected = ['USB\\VID_2E8A&PID_000F\\' + SERIAL]; self.session.poll()
        self.now += 10; self.hw.connected = [DEVICE]; self.session.poll()
        self.assertEqual(len(self.hw.loads), 1)

    def test_stop_start_does_not_clear_latch(self):
        self.session.poll(); self.session.poll(allow_program=False); self.session.poll()
        self.assertEqual(len(self.hw.loads), 1)

    def test_late_reboot_after_failure_cannot_repeat_write(self):
        self.hw.fail = True; self.session.poll()
        self.unplug()  # Windows reports absence, even if an unusually late reboot
        self.now += 30; self.hw.connected = [DEVICE]; self.session.poll()
        self.assertEqual(len(self.hw.loads), 1)

    def test_stopped_session_does_not_touch_status_or_program(self):
        self.hw.status = lambda p: self.fail('status accessed while stopped')
        self.session.poll(allow_program=False)
        self.assertEqual(self.hw.loads, [])

    def test_failure_requires_unplug_no_blind_retry(self):
        self.hw.fail = True; self.session.poll(); self.session.poll()
        self.assertEqual(len(self.hw.loads), 1)
        self.assertEqual(self.session.failures, 1)
        self.assertEqual(self.hw.checked, [])

    def test_unknown_firmware_never_flashes(self):
        self.hw.before = OLD.replace('eggbert-integrated-H8.1', 'other-device')
        self.session.poll(); self.assertEqual(self.hw.loads, [])

    def test_saved_and_active_recordings_refused(self):
        for status in [OLD.replace('IDLE, 0/', 'COMPLETE, 19200/'),
                       OLD.replace('IDLE', 'ARMED - WAITING FOR EVENT')]:
            with self.assertRaises(ValueError): check_ready(status)

    def test_multiple_devices_or_missing_usb_identity_refused(self):
        self.hw.connected.append('USB\\VID_2E8A&PID_0009\\FEDCBA9876543210')
        self.session.poll(); self.assertEqual(self.hw.loads, [])
        self.hw.connected = [DEVICE]; self.hw.port_list[0].serial_number = None
        self.session.poll(); self.assertEqual(self.hw.loads, [])

    def test_mismatched_serial_and_usb_refused(self):
        self.hw.port_list[0].serial_number = 'FEDCBA9876543210'
        self.session.poll(); self.assertEqual(self.hw.loads, [])

    def test_topology_change_before_flash_refused(self):
        def status(port):
            self.hw.connected.append('another device'); return OLD
        self.hw.status = status
        self.session.poll(); self.assertEqual(self.hw.loads, [])

    def test_enumeration_failure_does_not_clear_latch(self):
        self.session.blocked = True
        def broken(): raise OSError('enumeration failed')
        self.hw.devices = broken
        with self.assertRaises(OSError): self.session.poll()
        self.assertTrue(self.session.blocked)

    def test_reboot_requires_exact_version_and_healthy_both_models(self):
        check_result(NEW, FW)
        check_result(NEW.replace('LSM6DSV320X', 'LSM6DSV'), FW)
        for bad in [OLD, NEW.replace('OLED: OK', 'OLED: NOT FOUND'),
                    NEW.replace('LSM6DSV320X', 'UNKNOWN')]:
            with self.assertRaises(ValueError): check_result(bad, FW)

    def test_firmware_bundle_integrity_and_path(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); data = b'\x7fELF eggbert-integrated-H8.2'
            (root / 'eggbert.elf').write_bytes(data)
            manifest = {'file': 'eggbert.elf', 'version': 'H8.2',
                        'sha256': hashlib.sha256(data).hexdigest()}
            (root / 'firmware.json').write_text(json.dumps(manifest))
            self.assertEqual(Firmware.read(root).identity, FW.identity)
            (root / 'eggbert.elf').write_bytes(b'changed')
            with self.assertRaises(ValueError): Firmware.read(root)
            manifest['file'] = '../elsewhere.elf'
            (root / 'firmware.json').write_text(json.dumps(manifest))
            with self.assertRaises(ValueError): Firmware.read(root)

    def test_picotool_serial_target_and_no_success_without_verification(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'firmware.elf'; path.write_bytes(b'firmware')
            fw = Firmware(path, FW.version, hashlib.sha256(b'firmware').hexdigest(), FW.identity)
            hardware = Hardware(Path(d) / 'picotool.exe', fw, lambda v: None)
            with patch('batch_programmer.subprocess.run') as run:
                run.return_value = SimpleNamespace(returncode=0, stdout='Verifying Flash ... OK\n', stderr='')
                hardware.flash('COM5', SERIAL)
                args = run.call_args.args[0]
                self.assertEqual(args[-2:], ['--ser', SERIAL])
                self.assertIn('-v', args); self.assertIn('-x', args); self.assertIn('-f', args)
                self.assertNotIn('shell', run.call_args.kwargs)
                run.return_value.stdout = 'Load finished without verification'
                with self.assertRaises(RuntimeError): hardware.flash('COM5', SERIAL)
                run.return_value.returncode = 1
                with self.assertRaises(RuntimeError): hardware.flash('COM5', SERIAL)
                run.side_effect = subprocess.TimeoutExpired(args, 90)
                with self.assertRaises(RuntimeError): hardware.flash('COM5', SERIAL)

    def test_changed_firmware_prevents_write(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'firmware.elf'; path.write_bytes(b'wrong')
            hardware = Hardware('picotool.exe', Firmware(path, FW.version, 'a'*64, FW.identity), lambda v: None)
            with patch('batch_programmer.subprocess.run') as run:
                with self.assertRaises(RuntimeError): hardware.flash('COM5', SERIAL)
                run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
