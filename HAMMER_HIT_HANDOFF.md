## H8.2 update (November 13, 2026)

Keep the successful turn/back/pause arming gesture. Once armed, the first
calibrated low-g vector magnitude strictly above 7 g starts recording, with
no flight or minimum pulse-duration requirement. This replaces the earlier
1.5 g impact followed within 150 ms by 200 ms of near-weightlessness.
The armed tick stops and the recording cue plays on acceptance (when sound
is enabled). Handle the package gently after arming; a placement bump above
7 g can trigger too. The two-minute timeout and accessible middle-button
cancel remain. Before arming, impacts cannot start or prequalify a capture.

The same 19,200-sample window retains 0.5 seconds before threshold crossing
and 4.5 seconds including/after it. Downloads mark Impact, not confirmed
Freefall. No file-format or buffer-size change. Completed recordings remain
protected. Settings and serial status identify H8.2.

Tests cover the exact threshold, signed axes, diagonal vector magnitude,
single-sample hits without subsequent flight, lower handling acceleration,
pre-arm impacts, timeout, both V1/V2 recording windows, metadata and scores.
Validation passed: six Hammer native suites; full native app/settings/idle
suite; 71 rendered UI states with zero text overflows and 52 unchanged activity
snapshots; capture-format, dual-stream viewer, Hammer viewer and orientation
tests. The actual C trigger accepted the initial strike in all three supplied
representative trials (red bubble wrap, extra bubble wrap and foam).
Production ELF built on this PC with installed Arduino Pico SDK
2.2.1-develop / arm-none-eabi GCC 14.3.0; this differs from H8.1's documented
SDK 2.3.1 / GCC 15.2 environment. No board was flashed or tested with H8.2.
Physical wrapped-package/handling trials with H8.2 remain necessary.
The older sections below document prior builds and validation.

## H5 update (November 11, 2026)

The first five Home entries are HAMMER HIT, RECORD, ACCEL G, GYRO, MAZE.
The reordered menu build passed and was programmed with Picotool flash
verification OK on November 11, 2026. Completion shows MAX magnitude in g
and OVER 20g in milliseconds across the entire five-second recording, including
launch, landing, and bounces. Duration counts V2 high-g vector magnitudes strictly
above 20 g, using nominal 10417 micro-g/count and sample rate (3840 Hz).
Each qualifying sample contributes one sample period; displayed milliseconds are
rounded to one decimal. Chart zoom does not affect the totals. The website now has Normal view and
Hammer Hit analysis buttons; the latter shows the two metrics, a 20 g line,
and shaded high-g intervals across the full recording. V2 uses raw high-g
samples for duration and automatic low/high magnitude for MAX, matching H5.
V1 shows N/A / NEEDS V2 because its low-g sensor cannot reliably measure this
threshold. Clipping or capture errors show N/A with the existing warning.
Maximum magnitude retains the existing automatic low/high sensor selection.
Download or explicitly erase the one protected capture before another run.
Serial status identifies eggbert-integrated-h5 and includes over20_samples.
Build and all 11 ARM regression suites passed, as did the actual OLED layout
check (no text overflows, Hammer Hit first). H5 was programmed November 11
with Picotool flash verification OK on device 6A468A4BF7717CE3. Post-reboot
COM47 status confirmed integrated-h5, LSM6DSV320X ID 0x73, OLED OK and
paired FIFO integrity OK. Dr. Kohl tried H5 on the device and reported that
the results display worked successfully.

# Hammer Hit H4: turn, turn back, relaxed pause

Integrated November 5, 2026 on `master`, retaining Caleb's app catalog, scrolling
Home menu, Maze, and persistent sound/brightness settings. The latest behavior
comes from `codex/gyro-arming`, which already includes `codex/hammer-hit`.
`src/apps/hammer.c` registers a capture adapter like Record and Beam; acquisition,
gyro polling, protected buffers and incremental OLED transfers use shared services.

Prior H4 startup/flash checks on V2 do not establish physical gesture or impact
reliability for this combined firmware. The combined build has not been flashed.

## Use it

1. Choose HOME > HAMMER HIT > MID BEGIN before covering the buttons.
2. Finish wrapping/taping. Turn the whole package about one full turn in any
   comfortable direction/axis; listen for a chirp.
3. Turn it back the other way; listen for the second chirp.
4. Stop deliberate turning and pause comfortably for about two seconds.
   Small hand movements and brief bumps are allowed. A rising melody and
   steady green mean ARMED. A tick once per second confirms readiness.

There are no tosses. Serial status identifies eggbert-integrated-h4; the screen shows
turn and pause progress. A turn threshold is 270 degrees, so an approximate
full turn suffices. Reverse around the same general axis used for the first
turn. Each leg has six seconds. The final pause has eight seconds, accepts
rotation up to 45 deg/s and 0.65--1.4 g, accumulates two quiet seconds, and
retains pause progress through bumps of up to 350 ms. A longer bump resets
only pause progress. Partial gestures expire quietly. Missing sensor data
cannot complete the pause. These are initial, physically unvalidated settings.

## Recording behavior retained

- Two-minute armed timeout, middle-button cancel before a confirmed event.
- Impact followed by 200 ms of near-weightlessness confirms launch.
- Five seconds at 3840 Hz: 0.5 seconds before the original launch candidate,
  then 4.5 seconds including/after it. Confirmation doesn't move the window.
- V2 stores low/high raw accelerometers in EGG2; V1 stores EGG1 with clipping.
- Complete recordings are protected in RAM until explicit erase. Download
  before power-off or programming. The completion screen shows MAX g and
  reports clipping/data faults; no HIT/END judgment.
- Gyro is polled separately, never inserted into the accelerometer FIFO or
  .egg payload. It stops at confirmed recording, cancellation or completion.

## Validation

Run `python tools/run_hammer_native_tests.py` with Clang/GCC (`CC` can be a
full compiler path). It runs six suites:

- Gesture: both directions on all axes and diagonal, slower turns, hand tremor,
  brief/prolonged bumps, same-direction wrapping turns, small shakes, wrong
  reverse axis, partial/expired gestures, time gaps, clock wrap, launch/timeout.
- Actual firmware recording and lifecycle functions: both board formats,
  wrapped RAM rings, 19,200 samples, trigger offset 1920, high-g maximum,
  clipping, protected BEGIN, gyro setup failure, data timeout, cancellation,
  main-loop gyro arming, and stale acceleration during the pause.
- Actual IMU driver with SPI stubs, including simultaneous gyro/capture config.
- Actual sound module, scheduled display module, and Beam recording regression.

Native tests passed. The ARM firmware build passed with Pico SDK 2.3.1 / GCC 15.2.1. H4 was subsequently programmed and flash-verified, with post-reboot status confirmed.
Physical turn/back/pause ease, false activation while taping, gyro polling
cadence/FIFO timing, sounds, and full launch/landing still need trials on both
boards. No synthetic test guarantees the feel of the wrapped package.

## Build and program

Use Pico SDK 2.3.1, an ARM compiler, CMake and Ninja. Supply explicit paths:

```powershell
cmake -S . -B build -G Ninja -DPICO_NO_PICOTOOL=1 "-DPICO_SDK_PATH=C:/path/to/pico-sdk" "-DPICO_TOOLCHAIN_PATH=C:/path/to/arm/bin"
cmake --build build
```

Program the resulting `build/eggbert_bringup.elf` with Picotool as documented
in PROGRAMMING_EGGBERT.md. Do not reuse an old binary or build cache. Avoid
very long Windows compiler paths. H4 was programmed to device 29DA76A03DEC6A11 and verified on COM5. Further hammer trials remain to do.

## Contest discussion and next work

See [CONTEST_RESEARCH_HANDOFF.md](CONTEST_RESEARCH_HANDOFF.md) for the latest
hammer-launch contest setup, 43 g device mass, NASA research links, measured
throw/catch capture, example calculations and unresolved scoring decisions.

## Combined master verification (November 5, 2026)

- Firmware builds with Pico SDK bundled in Arduino-Pico 5.6.0 and ARM GCC 14.3.0.
- Eleven ARM simulator suites pass: gesture, real recording/start/cancel paths,
  IMU driver, sound (including muting), incremental OLED, Beam, app integration,
  Home navigation, settings recovery, Maze session and calibration cancellation.
- Production OLED rendering passed with no text overflows. All 51 prior activity
  snapshots match byte for byte; Hammer Hit states were also rendered and inspected.
- Native Windows test binaries were denied execution on this computer. Use
  `python tools/run_arm_regressions.py --cc <arm-none-eabi-gcc-path>` as the
  repeatable fallback. Install `unicorn` and `pyelftools` into
  `tmp/beam-test-runtime` or the active Python environment.
- The unchanged exhaustive 1000-seed Maze algorithm test exceeded the simulator
  instruction limit; the Maze app/session and data-preservation tests passed.
- No board was flashed with this combined build. Verify wrapped turn/back/pause,
  false arming while taping, a complete launch/landing recording, download,
  sound-off behavior and saved brightness on V1/V2 hardware before relying on it.
