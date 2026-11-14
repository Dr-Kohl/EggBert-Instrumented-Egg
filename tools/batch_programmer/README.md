# EggBert batch programmer - H8.2

## At school: download, extract, start

1. Download the ready-to-run ZIP from `downloads/EggBert-H8.2-Windows.zip` in this
   repository. Extract ALL files to a normal local folder. Do not run inside the ZIP.
2. Double-click `Start EggBert Programmer.cmd` or `EggBertBatchProgrammer.exe`.
   No Python, Arduino IDE, or Picotool installation is needed for this package.
3. Close the capture webpage and serial monitors. Download any recordings first.
4. Click **Start**, then plug in ONE EggBert. Keep it connected while programming.
5. Wait for the green **SUCCESS - UNPLUG THIS EGGBERT** message, unplug, then
   connect the next board. Windows may need a moment to detect each connection.
6. Click **Stop** when done. Stop and window-close let a current board finish;
   they never interrupt an active flash. No new board is programmed while stopped.

The window shows the fixed target H8.2. It starts stopped and never programs
anything just because the window was opened. It uses the same H8.2 firmware
already built and tested for Hammer Hit's above-7-g trigger, with no flight gate.
The complete package works offline on 64-bit Windows 10/11; normal USB device
drivers must already work. On first use, Windows may take time to set up USB.

## What success means

Each board must identify itself over serial as EggBert before anything is written.
The tool targets Picotool by that exact 16-digit USB serial, verifies flash,
then waits for the same serial to return and report `eggbert-integrated-H8.2`,
a supported LSM6DSV/LSM6DSV320X IMU and OLED OK. A version alone is not enough
for a green success. The firmware checksum is checked at startup and again
before every flash. The tool never downloads an automatic/latest firmware.

After every attempt, including a failure, the tool waits for physical unplugging.
It watches USB devices in both application and BOOTSEL modes, so a normal reboot
is not mistaken for a new board. All Raspberry Pi/Pico USB devices must be absent
for at least 1.5 seconds before the next board is accepted. Do not attach other
Picos during a batch. A second Pico makes the tool wait rather than guess.

Boards already reporting H8.2 are still programmed with this exact verified ELF.
Each serial is attempted only once per run, even if unplugged and reconnected.
This also protects against delayed USB reboots after a failed/uncertain result.
For a deliberate retry, inspect the log, unplug the board, and close/reopen the
program. Stop/Start does not clear this protection.
No OTP commands, whole-chip erase, calibration reset or settings reset are used.
Flashing/reboot still clears RAM recordings. A reported recording or active capture
causes refusal: download your data and return to HOME before retrying.

## If a board needs attention

- Red message: unplug that board and keep it aside. Read the message/log; do not
  count it as programmed. There is no automatic write retry after an uncertain result.
- Busy serial port: close serial monitors, browsers and other programmer windows.
- A board in BOOTSEL only cannot identify itself as EggBert. It is deliberately
  NOT flashed automatically. Use the manual procedure in PROGRAMMING_EGGBERT.md
  for blank/recovery boards, then use this batch tool for normal firmware updates.
- If Picotool cannot reboot an older EggBert, use the documented manual 1200-baud
  recovery procedure. The batch tool does not blindly reset an unknown board.
- If a board reports the right version but an IMU or OLED fault, inspect the board.
- If USB and serial identities differ, use the manual programmer and save the log.
- If Windows says a file is missing, extract the entire ZIP again. The `_internal`
  and `picotool` folders must stay next to the EXE.

**Open logs** opens `%LOCALAPPDATA%\EggBert\BatchProgrammer\logs`.
Each run writes a CSV with board serial, old/target firmware, firmware SHA-256,
success/failure and reason, plus a detailed text log. Records stay on that PC;
the programmer sends nothing to GitHub or any other network service.

## Tomorrow's first-board check

Software simulation and package checks cannot establish the USB behavior of the
school PC. First program one accessible board, confirm Settings says **FW H8.2**,
IMU/OLED work and the log records success. Unplug and connect a second board.
Confirm one successful cycle per connection before doing the rest. Hardware
checks for the H8.2 hit behavior itself are still separate physical trials.

## Source and rebuilding

Runtime source: `batch_programmer.py` and `windows_usb.py`. Run tests from the
repository root: `python tests/batch_programmer_test.py` (no board is touched).
For source use, install `requirements.txt` and supply the package directory:

    python tools/batch_programmer/batch_programmer.py --package-dir path/to/extracted/EggBertBatchProgrammer

The packaged EXE has no Python dependency. To rebuild it on Windows with Python
3.12/3.13 and Tcl/Tk available, install `requirements-build.txt`, then run:

    python tools/batch_programmer/build_windows.py --firmware path/to/eggbert_bringup.elf --picotool path/to/picotool.exe --output-dir build/batch-release

The builder pins the known H8.2 SHA-256, includes the Picotool DLL and license
notices, and emits the ZIP plus its SHA-256. Picotool must sit beside its original
`libwinpthread-1.dll`, `LICENSE.TXT` and `package.json` when building.
The EXE/source `--check` option checks the firmware/package and Picotool version
without opening serial ports, enumerating boards, resetting or programming.
For source-only use, Picotool is found in the package first, on PATH, or in the
current user's Arduino/Pico SDK folders. `EGGBERT_PICOTOOL` can select an explicit
path; `EGGBERT_LOG_DIR` can select a writable log directory.
