# EggBert RP2354A board bring-up

HOME 2 is ordered BEAM, CALIBRATE, RAW XYZ. In calibration, the top button
(UP EXIT) cancels at any stage before saving and keeps the previous calibration.
After six faces the candidate stays in RAM; press MID SAVE to deliberately
install it. Exiting earlier discards partial or completed unsaved readings.
The cancel/explicit-save ARM harness is generated from the actual wizard
functions by `tools/make_calibration_cancel_test.py`; compile its output with
entry `calibration_cancel_test`, then use `tools/run_arm_test.py`.

## Shared firmware: version 1 and version 2 bring-up

The same firmware now detects LSM6DSV (`WHO_AM_I=0x70`) or LSM6DSV320XTR
(`0x73`) over the existing SPI wiring. Startup resets the identified IMU and
verifies coherent burst reads, 120 Hz +/-2 g live acceleration, and the
on-demand gyro configuration. Unrecognized IDs or failed configuration are
reported as an initialization failure; sensor operations remain disabled.
The USB startup message and `status` command identify the detected model.

Version 2 now records **both accelerometers during Record / Drop Test**:
low-g at +/-16 g and high-g at +/-320 g, both at nominal 3840 Hz. The same
five-second window retains 19,200 paired XYZ readings per sensor. Version 1
continues low-g recording. Accel, Level, Gyro, Pendulum, Friction, Beam,
and orientation keep their existing ranges, rates and units. Gentle Catch now
uses both accelerometers on version 2 for its peak measurement.
High-g is powered down outside version 2 drop recordings and Gentle Catch. Normal downloads use
EGG2 for paired recordings, preserving both raw streams plus low-g calibration;
version 1 and Beam retain EGG1. The website opens both formats and provides an
Automatic / Low-g / High-g selector and CSV export of both streams plus the
automatic trace and per-sample source.
Register compatibility was checked against ST's official
[LSM6DSV320X driver](https://github.com/STMicroelectronics/lsm6dsv320x-pid).

Version 2 also adds a 3000 Hz-labeled buzzer driven by GPIO22 through an AO3400A
low-side MOSFET, with a 10 kOhm gate pulldown and SS14 flyback diode to Out+.
The user confirmed GPIO22 is unused on version 1, so future button sounds may
drive the same pin on both boards. Initialize it low/off and use nonblocking
sound timing. The user confirmed it is a passive piezo speaker requiring
a waveform. See the supplied [speaker schematic](docs/hardware/speaker-gpio22-schematic.png).
Button and status sounds are implemented with hardware PWM and timer alarms.

### Version 2 verification

`tests/imu_driver_test.c` exercises the actual driver with a simulated SPI
device: both IDs, startup retries, bounded reset, configuration verification,
signed live reads, gyro freshness, low-g FIFO tags, overrun reporting, beam
settings, orientation register restoration, and rejection of unknown devices.
Build using the ARM compiler (PowerShell; run from this checkout):

```powershell
& "$toolchain/arm-none-eabi-gcc.exe" -std=c11 -Wall -Wextra -Werror -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -O2 -Itests/imu_stubs -Iinclude -nostartfiles '-Wl,-e,imu_driver_test' '-Wl,-Ttext=0x10000000' --specs=nosys.specs tests/imu_driver_test.c src/lsm6dsv.c -o build/imu-driver-test.elf -lm
python tools/run_arm_test.py build/imu-driver-test.elf
```

Set `$toolchain` to your ARM compiler bin directory; the test runner needs
Unicorn and pyelftools. Simulated tests do not establish physical accuracy.
On each board, check `status`, approximately 1 g stationary magnitude, axis
signs, Level, stationary/rotating Gyro, and each activity before classroom use.

Verified October 20, 2026: firmware built and flash verification passed on the
connected version 2 board (USB serial COM37). `status` identified LSM6DSV320X,
ID `0x73`, with high-g disabled. Stationary raw XYZ was approximately
`445, -810, 16134` at 16384 counts/g (magnitude about 0.986 g). The existing
orientation mode delivered 183 quaternion/acceleration/gyro records in about
three seconds, then stopped and restored normal +/-2 g live readings without
reported FIFO overflow. Both-model simulated driver tests, beam ARM tests and
viewer orientation/USB lifecycle tests passed. The viewer test was updated to
recognize the existing 7200-sample beam file as well as 19200-sample drop files.
Physical version 1 regression and on-device menu/activity checks remain to do.

### Step 2: dual-stream recording (October 21, 2026)

The normal on-device Record / Drop Test controls, stillness/freefall trigger,
0.5-second pre-trigger window, and protected capture are unchanged. Both raw
streams share ring indices, so trimming and wrap preserve their correspondence.
Each raw buffer is 115,200 bytes; combined storage is 230,400 bytes (225 KiB).
Low-g calibration is applied to trigger logic and display, while raw low/high
counts are preserved. Calibration is frozen with the recording; later changes
cannot reinterpret it. High-g does not inherit low-g calibration.

The FIFO decoder accepts low-g tag `0x02` and high-g tag `0x1D`, matches their
two-bit `TAG_CNT` time slots, and checks continuity modulo four. A partial first
slot can be discarded during startup. After synchronization, a missing partner,
duplicate, unexpected record, transfer failure, or FIFO overflow stops recording
and marks it invalid. This follows ST's
[AN6119, section 9.4](https://www.st.com/resource/en/application_note/DM01076787.pdf).
Slots establish digital sampling correspondence; separate filter delays and
high-g offsets still need characterization before automatic trace selection.

Engineering commands (no existing protected capture may be overwritten):

- `dualtest`: from HOME on v2, record 19,200 pairs without requiring a throw.
- `dualtest ring`: roll for six nominal seconds, then exercise the normal
  pre-trigger trim and post-trigger window. Calls the normal READY/EVENT display
  updates when an OLED is detected; `status` reports OLED detection separately.
- `status`: report consumed low/high records, completed pairs, startup discard,
  integrity errors, peak FIFO occupancy, and host elapsed time. Consumed pairs
  can exceed stored pairs because the driver reads batches and the ring rolls.
- `download raw`: engineering-only EGD2 frame from a completed dual recording.
  As of step 3, normal `download` emits EGG2 for paired captures and EGG1 for
  single-sensor captures. Failed paired captures carry an explicit invalid flag.

EGD2 is not a website `.egg` format. Its 32-byte little-endian header is:
magic `EGD2`, u8 version (1), u8 flags (bit 0 overflow, bit 1 integrity failure),
u16 header size (32), then u32 fields for nominal rate, pair count, low-g
micro-g/count (488), high-g micro-g/count (10417), record stride (12), and payload
CRC-32. Each record contains six signed int16 values: low XYZ followed by high
XYZ, in chronological order. Rates and sensitivities are nominal datasheet
values, not a precision clock or device calibration. No gyro data is recorded.

After either test, save and verify both representations with:

```powershell
./tools/read_capture.ps1 -Port COM37 -OutputDirectory build -Prefix dual-bench
python tools/check_dual_capture.py build/dual-bench.egd2 --legacy build/dual-bench.egg --report build/dual-bench-report.json
```

Bench verification on the connected v2 board passed five-second paired capture,
FIFO tag continuity, and buffer wrap / pre-trigger retention. Both exports had
valid CRCs and exactly 19,200 samples per stream; the rolling test's EGG1 trigger
offset was 1920. No sequence errors, unexpected tags, transfer errors or FIFO
overflow were reported. At rest the high-g channel has visibly greater noise
and offset than low-g, reinforcing the need to keep low-g as the primary signal.
The connected bench board reports `OLED: NOT FOUND`: READY/EVENT rendering was
therefore skipped, and capture timing with an attached OLED is not yet verified.
Real impact waveform accuracy, channel delay characterization, and physical
version 1 regression remain unverified.

### Step 3: dual-stream downloads and viewer (October 21, 2026)

Normal v2 drop downloads are `.egg` files with an EGG2 header and both raw
streams. The viewer starts with Low-g selected; choose High-g to inspect the
other sensor. The selection preserves the time window and measurement cursors.
Axis/magnitude views, zoom, event focus, and vertical scales up to +/-320 g work
with either stream. Flight estimates always use low-g to avoid high-g noise.
This does not combine the channels or automatically switch between them.

Save .egg preserves the original binary file. Dual CSV exports include time,
calibrated low-g XYZ/magnitude, factory-scaled high-g XYZ/magnitude, all six raw
counts, per-sample clipping indicators, and a capture-valid indicator. EGG1 CSV
columns remain unchanged. Header CRC protects ranges and calibration metadata;
payload CRC protects raw samples. Data-loss flags and CRC failures are visibly
reported and invalidate the CSV indicator. See [EGG_FORMAT.md](EGG_FORMAT.md).

The included Version 2 stationary sample is an actual 19,200-pair bench capture,
not an impact experiment. Its EGG2 payload was compared byte-for-byte with the
engineering raw export. Tests cover C-to-JavaScript format compatibility,
positive/negative calibration and clipping, metadata corruption, payload CRC,
truncated/fragmented serial transfers, old EGG1 files, source switching and CSV.
Run `tests/egg_format_test.c` under `tools/run_arm_test.py`, dumping symbol
`egg2_test_frame` (128 bytes) to `build/egg2-format-fixture.egg`, then run:

```text
node tests/dual_viewer_test.cjs
node tests/orientation_test.cjs
```

First firmware for the JLCPCB Instrumented Egg board. It tests the three LEDs,
three pushbuttons, LSM6DSVQTR IMU, and the 128x64 I2C OLED while printing results
over USB CDC.

BEAM now records 15 seconds at 480 Hz and +/-4 g, triggered by a sustained
0.2 g change after settling. See [Beam recording](BEAM_RECORDING.md) for the
student workflow, file flags, and frequency-estimate limitations.

## Pin assignment

| System | RP2354A GPIO | Notes |
|---|---:|---|
| Green LED | 1 | high = on (assumed; reverse if board indicates otherwise) |
| Yellow LED | 2 | high = on (assumed) |
| Red LED | 12 | high = on (assumed) |
| SW5 | 0 | internal pull-up, pressed = 0 |
| SW2 | 3 | internal pull-up, pressed = 0 |
| SW3 | 13 | internal pull-up, pressed = 0 |
| OLED SDA | 24 | I2C0, external 4.7 kOhm pull-up fitted |
| OLED SCL | 25 | I2C0, external 4.7 kOhm pull-up fitted |
| LSM6DSV MISO / SDO | 16 | SPI0 RX; U5 pin 1 |
| LSM6DSV CS | 17 | SPI0 CS; U5 pin 12 |
| LSM6DSV SCK / SPC | 18 | SPI0 SCK; U5 pin 13 |
| LSM6DSV MOSI / SDI | 19 | SPI0 TX; U5 pin 14 |
| LSM6DSV INT2 / INT1 | 20 / 21 | U5 pins 9 / 4; configured as inputs |

The OLED driver targets a 128x64 SSD1306 at `0x3C`. If the display does not
respond, rebuild with `-DOLED_I2C_ADDRESS=0x3D`.

## Build

Install the Raspberry Pi Pico SDK and an ARM embedded toolchain, then configure:

```powershell
cmake -S . -B build -DPICO_SDK_PATH="C:/path/to/pico-sdk"
cmake --build build
```

`eggbert_bringup.uf2` is produced in `build/`. Hold BOOTSEL while connecting USB,
then copy that UF2 to the RPI-RP2 drive. The project defaults to the Pico SDK
bundled with the installed Arduino-Pico core when it is present.

## Expected behavior

At boot, each LED lights for 300 ms. The OLED then shows the rotated portrait
**HOME** screen if the IMU answers with `WHO_AM_I = 0x70`. The left yellow rail
uses yellow-on-black power icons: a vertical battery gauge on battery power or a
plug when USB power is present. Its one-letter developer marker shows the current
state (`I`, `A`, `R`, `E`, or `D`). Acceleration values continue to be reported
over USB serial at 4 Hz in raw counts (0.061 mg/LSB at the live-read scale).

### On-device capture workflow

EggBert owns capture control. Use the OLED and three buttons to navigate:

```text
Home -> Record -> Drop Test -> Arm -> Hold Still -> Ready -> Capture Complete
```

The top button moves the selection up, the middle button selects it, and the
bottom button moves it down. At `CAPTURE COMPLETE`, the capture is protected in
RAM: connect the USB webpage to download it, or use `SELECT = OPTIONS` and the
two-step `ERASE CAPTURE` confirmation before rearming. A new capture cannot
overwrite a completed one accidentally.

`Home -> Raw XYZ` is a live signed-count diagnostic screen. It uses the normal
120 Hz, +/-2 g live-read mode and refreshes X/Y/Z every 100 ms; the middle
button returns home. Each capture start also reads back IMU `CTRL8` and refuses
to proceed unless it reports `0x03`, the expected +/-16 g capture range.

### Gyroscope

`Home -> GYRO` opens the **PEAK** view. Put EggBert down and hold it still
while `HOLD STILL / ZEROING` is displayed. After a one-second settling delay,
the firmware averages 120 fresh stationary samples to estimate gyro offset.
Movement restarts that averaging window. This temporary zero is taken again
each time GYRO is opened; the accelerometer calibration is separate.

The screen shows signed X/Y/Z angular rates in **deg/s**. The top button switches
**PEAK / LIVE**, the middle button clears all peaks, and the bottom button exits.
After reset, `WAIT` is displayed for 750 ms so pressing the button does not
become part of the next trial. Reset retains the stationary zero.

Rotate about an axis, stop, and read its peak. Each peak retains the largest
absolute speed with its original sign (for example, -500 replaces +200).
Peaks continue tracking in both views until reset. `LIMIT` warns that a raw
axis approached the nominal +/-2000 deg/s range; this warning is retained
with the peaks until reset. One revolution per second is 360 deg/s.

The gyro runs at 240 Hz only while this screen is open, with fresh-data polling
separate from the 150 ms OLED refresh. It powers down on exit. OLED transfers
and loop scheduling can skip sensor updates; this is an exploratory peak
readout, not a lossless gyro recording. The existing accelerometer-only FIFO,
`.egg` file format, and website downloads are unchanged. Gyro capture and
orientation fusion are separate future additions.

The pure session logic can be checked with a native C compiler:

```text
clang -std=c11 -Wall -Wextra -Werror -Iinclude tests/gyro_session_test.c -o gyro_session_test.exe
gyro_session_test.exe
```

### Gentle Catch game

`Home -> Catch -> Gentle` starts a feedback-only catching challenge. EggBert
waits for 100 ms of confirmed freefall, detects the following catch, measures
its first 0.15 seconds, and displays the peak **axis** acceleration in a
double-size font. `CLIPPED / 16G+` means an axis reached the sensor's
measurement limit, so the true impact was at least that large. The result stays
on screen until the next confirmed freefall. After EggBert is held still near
1 g for 0.5 seconds, its green LED turns on to show it is ready for the next
throw without clearing that last result. Its yellow or red outcome LED remains
on for five seconds. Press the middle button at any time to leave the game.

The LED score is solid yellow below 5 g (gentle), blinking yellow from 5 to
under 10 g (firm), and blinking red at 10 g or above or for any clipped catch.

The game never writes the RAM capture buffer, so it cannot overwrite a saved
`.egg` capture that is awaiting download or deliberate erasure.

USB CDC is for download and diagnostics, not capture arming. Type `help` in a
serial terminal to see the available commands:

| Command | Result |
|---|---|
| `arm` (or `start`) | Reports that arming must be done from the EggBert menu. |
| `stop` | Reports that capture control is restricted to the EggBert menu. |
| `status` | Reports state, stored sample count, and whether the IMU FIFO overflowed. |
| `download` | Sends the completed capture in the `EGG1` binary format. |
| `clear` | Reports that erasing must be done from the EggBert menu. |

This milestone uses the IMU FIFO, a 32-sample watermark interrupt on IMU INT1
(GPIO21), and a 115,200-byte RAM ring buffer. After one second of magnitude
between 0.8 g and 1.2 g, it triggers only on magnitude below 0.45 g (freefall).
The downloaded recording is always five seconds: 0.5 seconds before freefall
and about 4.5 seconds afterward. This removes the menu/arming delay and avoids
an accidental trigger from the throwing motion. Those first-pass thresholds are
intended to be tuned from real tests. It does not write flash. After a capture
stops, the firmware returns the IMU to its existing 120 Hz, +/-2 g live-read
configuration.

## Capture viewer web app

The static viewer is in [`docs/`](docs/), so this repository can be published
directly with GitHub Pages. It connects through Web Serial in Chrome or Edge,
downloads the completed RAM capture, verifies its CRC, and plots X/Y/Z plus
acceleration magnitude. It can also open and save raw `.egg` capture files, or
export a plain Excel-friendly CSV with time, X/Y/Z acceleration, and magnitude
in g. In Chrome or Edge, each export opens a Save As dialog with a timestamped
EggBert filename suggestion that the user can edit and save anywhere.

For freefall captures with a detected impact, the viewer also shows detected
flight time and an estimated peak height, calculated as `gT^2 / 8`. The height
assumes a near-vertical throw with approximately equal release and catch height.

The `download` USB command sends an `EGG1` version-1 binary frame: a 32-byte
little-endian header followed by chronological raw signed 16-bit X/Y/Z samples.
The header contains the sample rate, counts-per-g conversion, event marker,
post-event sample count, flags, and a payload CRC-32.

### Live orientation viewer

The GitHub Pages viewer also has a live 3D orientation panel. It uses the
actual enclosure meshes in `docs/models/eggbert-bottom.stl` and
`docs/models/eggbert-top.stl`; do not replace them with a generic box model.
After Web Serial connects, Start orientation sends `orientation fused` and
receives 60 Hz `Q2` records with quaternion, acceleration, and gyro data.
Start resting in any position; the viewer waits for stable gravity readings,
identifies the resting side, and displays actual tilt. **Recenter heading**
resets only the viewing direction. **Stop** sends `orientation off` and releases the USB serial
port, so a later session starts with a fresh Connect action.

CAD axes (sensor X/Y have the opposite directions; Z agrees):

| Positive axis | Physical direction |
|---|---|
| +X | USB-port side |
| +Y | button side (under the buttons) |
| +Z | out through the screen face |

Physical checks confirmed sensor +Z with the screen up and sensor +Y with
buttons down. The CAD-to-sensor mapping requires a Z=180-degree rotation.
The viewer applies this mapping and converts the sensor's Z-up world to
Three.js Y-up. Heading is relative and may drift without a magnetometer.
Older firmware retains a tilt-only fallback. See `ORIENTATION_PROTOCOL.md`.

## Deliberate exclusions

GPIO16–21 are reserved for the LSM6DSV IMU; GPIO29 is the OUT+ voltage sense;
the QSPI pins are flash signals. This bring-up project does not drive them.

### Step 4: automatic acceleration selection

The viewer defaults paired recordings to Automatic. A sample switches the whole
XYZ vector to high-g when any calibrated or raw low-g axis reaches 12 g.
It stays on high-g until every high-g, calibrated low-g and raw low-g axis is
below 8 g. Thresholds apply to axes rather than vector magnitude. The raw
low-g guard prevents calibration from masking the approach to clipping.
No blending, offset matching or filter-delay correction is applied; independent
sensor offsets and response can produce steps at transitions. Low-g stillness
and freefall detection are unchanged. Version 1 retains its low-g clipping.

Gentle Catch uses the same hysteresis on version 2 and reports a peak axis
up to the high-g range instead of stopping at 16 g. Its result uses a wider
integer without allocating another sample buffer; large OLED readings use whole
g rather than decimals to fit. FIFO failures stop the challenge without changing
any protected recording. The five-second recording buffers remain 230,400 bytes.

The .egg format and original bytes are unchanged. CSV preserves its first 18
dual-stream columns and appends Auto X/Y/Z/magnitude, Auto source (low/high),
and Auto clipped. Source selection is independent of which chart is displayed.
Old single-stream CSV remains unchanged. Existing EGG2 recordings also gain the
automatic trace. Gyro and the other low-g activities keep their existing settings.

Validation includes ARM boundary/overflow tests for the selector and wider peak
conversion, plus synthetic viewer tests for an abrupt 100 g impact, hysteresis,
clipping, source selection and legacy downloads. Physical impacts above 16 g,
sensor alignment/delay, and Gentle Catch menu trials still need bench validation.

Step 4 bench verification (October 21): flash verification passed on COM38,
LSM6DSV320X 0x73 and OLED OK. A new 19,200-pair capture completed with no FIFO
overrun, sequence/tag/I/O errors; EGG2 header/data CRCs and raw-stream identity
passed. Peak switching is tested synthetically; this stationary bench recording
does not validate real high-g impacts or Gentle Catch scoring on the device.

### Step 5: passive piezo sounds (GPIO22)

The same firmware drives GPIO22 on both boards; version 1 has no fitted speaker.
The GPIO starts low and returns to SIO output low after each cue. PWM runs at
approximately 50% duty using a 1 MHz counter derived from the actual system
clock. No sample buffer, blocking sleep or per-waveform interrupt is used.
Alarm callbacks advance notes and stop the output even during display/USB work.
Status cues preempt button clicks; a failed alarm allocation immediately silences
the pin. All sound control calls are on core 0.

Startup plays G6-B6-D7-B6-G7 (1.57-3.14 kHz) over 900 ms alongside the three-LED sequence.
It adds no startup delay and returns GPIO22 low when finished.

Cues: button 3000 Hz / 12 ms; ready 2400 then 3000 Hz; recording event 3600 Hz /
25 ms; complete 2400 then 3000 Hz with a gap; error two 1800 Hz notes. Record
and Gentle Catch get ready/event/result feedback. Ignored buttons during a
recording stay silent. Beam has no ready/event tones during acquisition, to
avoid exciting the beam mechanically; completion sounds after sampling stops.

At idle HOME, serial `soundtest` plays the completion cue; `soundoff` stops sound.
`status` reports the active cue (-1 means silent). Soundtest is refused during
capture or fused orientation and outside HOME. It does not change saved data.

ARM tests compile the actual sound module against PWM/timer stubs and exercise
all sequences, frequency/divider/duty, priority/preemption, stale callbacks,
cancellation, pin-low termination and alarm allocation failure. Build with the
same toolchain as the IMU tests, using `-Itests/sound_stubs -Itests/imu_stubs
-Iinclude`, entry `sound_test`, and sources `tests/sound_test.c src/sound.c`.

Verified on COM38: flash verified; IMU and OLED OK; user confirmed the test was
audible; status changed from cue 3 to silent automatically. Rolling dual capture
with ready/event/completion cues retained 19,200 pairs, trigger offset 1920,
no FIFO overrun/sequence/tag/I/O errors, and matching EGD2/EGG2 payload CRC
3c2b0ea0. Audible preferences and physical button/menu trials remain user checks.
A piezo can introduce mechanical vibration into acceleration readings; this
checks acquisition integrity, not absence of acoustic/mechanical artifacts.
