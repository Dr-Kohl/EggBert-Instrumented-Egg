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
