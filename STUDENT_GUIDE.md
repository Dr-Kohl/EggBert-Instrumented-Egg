# Understanding the EggBert code

EggBert connects a physical experiment to measurements you can inspect. Its
firmware reads motion sensors, manages the buttons and display, and retains a
recording in RAM. The browser viewer downloads or opens that recording, converts
counts into physical units, and plots it. This guide describes the existing
implementation so you can follow that path before changing it.

## Start with these files

| File | Responsibility | Good place to begin |
| --- | --- | --- |
| [src/main.c](src/main.c) | Application state, activities, calibration, capture, USB commands, menus | `main`, `service_capture`, `process_capture_sample` |
| [include/board.h](include/board.h) | GPIO assignments and bus settings | Find how a schematic net becomes a software pin |
| [src/lsm6dsv.c](src/lsm6dsv.c) | IMU identification, configuration, SPI reads and FIFO decoding | `lsm6dsv_init`, `lsm6dsv_dual_fifo_read` |
| [include/lsm6dsv.h](include/lsm6dsv.h) | Sensor driver interface and sample structures | Distinguish raw XYZ samples from quaternions |
| [include/accel_select.h](include/accel_select.h) | Low/high-g selection for Gentle Catch | `accel_select_high` |
| [include/egg_format.h](include/egg_format.h) | EGG2 header encoding and integer calibration | `egg_correct_low`, `egg2_header` |
| [src/ssd1306.c](src/ssd1306.c) | Display framebuffer, fonts, rotated coordinates and I2C transfer | `ui_pixel_unchecked`, `ssd1306_show` |
| [src/sound.c](src/sound.c) | Passive speaker waveforms and timed cues | `sound_play`, `next_note` |
| [include/gyro_session.h](include/gyro_session.h) | Gyro settling, bias estimation and peak angular rates | `gyro_session_sample` |
| [include/beam_trigger.h](include/beam_trigger.h) and [include/beam_frequency.h](include/beam_frequency.h) | Beam trigger and frequency estimate | Baseline settling, sustained trigger, autocorrelation |
| [docs/app.js](docs/app.js) | File validation, USB, plots, sensor selection and CSV export | `parseEgg`, `automaticStream`, `displayCapture` |
| [docs/orientation.js](docs/orientation.js) | Quaternion math, pose validation and heading recentering | `parse`, `absolute`, `stability` |
| [tests](tests) and [tools](tools) | Regression checks, simulated hardware and development utilities | Read a test alongside the function it exercises |

The `docs` directory is also the GitHub Pages website source. The firmware is
built by [CMakeLists.txt](CMakeLists.txt); its four C translation units are
`main.c`, `lsm6dsv.c`, `ssd1306.c`, and `sound.c`. Several small algorithms live
in headers as `static inline` functions, so changing a header can change firmware.

## Follow one recording

1. **Start and identify the board.** `main` initializes hardware. The driver reads
   `WHO_AM_I`: `0x70` identifies the v1 LSM6DSV; `0x73` identifies the v2
   LSM6DSV320X. Both use the same SPI wiring. An unknown ID is an error, not
   permission to guess a register layout.
2. **Arm.** `start_capture` refuses to overwrite a completed capture, snapshots
   the current low-g calibration, selects the recording range, and starts FIFO
   acquisition. The UI state describes the screen; the capture state describes
   acquisition progress. They are related but are not interchangeable.
3. **Read batches.** The IMU FIFO is a small hardware queue. Its watermark
   interrupt sets a flag; `service_capture` drains it from the main loop, with
   polling as a backstop. This avoids doing SPI transfers in the interrupt.
4. **Pair the v2 streams.** `lsm6dsv_dual_fifo_read` matches low/high records by
   their two-bit time-slot tags and checks modulo-four continuity. A pair can
   straddle two reads. Missing or duplicate records after synchronization mark
   the acquisition faulty rather than silently shifting one sensor's timeline.
5. **Trigger and retain history.** `process_capture_sample` uses calibrated
   low-g acceleration for trigger decisions. `store_capture_sample` advances a
   ring buffer. At the event, `retain_pretrigger_window` retains the desired
   history plus the trigger sample, then recording fills the post-event window.
   At nominal 3840 Hz, the five-second drop record holds 19,200 samples with
   a 0.5-second pre-trigger allowance.
6. **Protect the result.** Completion stops acquisition and preserves the RAM
   data for download. Erasing is an explicit action. Protection is not permanent
   storage: switching off or rebooting loses recordings that have not been saved.
7. **Download and interpret.** V2 drop recordings use EGG2: each sample contains
   raw low XYZ and high XYZ, plus the header's frozen low-g calibration. V1 and
   Beam use EGG1. `parseEgg` checks metadata and CRCs, converts units, and builds
   the viewer's traces. USB transfer timing does not determine sample timing.

The two v2 arrays share ring indices. Each array uses 115,200 bytes, or
230,400 bytes together, before other application storage. Sample pairs are
aligned digitally; that does not prove equal sensor/filter delay.

## Keep the units straight

| Mode or value | Units and interpretation |
| --- | --- |
| Normal live acceleration | Signed sensor counts at nominal 16,384 counts/g, range +/-2 g |
| Drop low-g acceleration | Nominal 2048 counts/g, range +/-16 g |
| Drop high-g acceleration | Nominal 0.010417 g/count, range +/-320 g; factory scaling, without the user low-g calibration |
| Beam acceleration | Nominal 8192 counts/g, range +/-4 g, 480 Hz for 15 seconds |
| Gyro | Raw counts at 0.07 degrees/second per count; session calculations use millidegrees/second |
| Calibration gain | Q16 fixed point: 65536 represents 1.0 |
| Orientation quaternion | Serial order w,x,y,z, scaled by 1,000,000; browser order x,y,z,w |

Do not divide every raw acceleration by the same constant: ranges change between
activities. Convert counts using the active configuration or saved metadata.
Calibration offsets are stored in live +/-2 g units and rescaled for recording.
The calibration wizard keeps a candidate in RAM until explicit SAVE; two
reserved flash sectors retain alternating CRC-checked calibration records.

An accelerometer reads roughly 1 g when held still and near zero in freefall.
Its XYZ magnitude is `sqrt(x*x + y*y + z*z)`, while a peak-axis value is
`max(abs(x), abs(y), abs(z))`. They are different measurements. A resultant
magnitude can exceed an individual axis's full-scale value without proving
that axis is unclipped; check the raw axes and clipping flags.

## Why there are two accelerometers

The low-g sensor offers finer resolution for modest acceleration. The high-g
sensor extends the measurable range during impacts. V2 records both, even while
the viewer displays only one automatic trace.

The automatic trace enters high-g when any calibrated or raw low-g axis reaches
12 g. It returns to low-g only when all three axes on both sensors are below
8 g, including the raw low-g guard. The separate thresholds are **hysteresis**:
they prevent repeated switching near one boundary. All three axes switch
together so one displayed vector uses one sensor. The browser counterpart is
`automaticStream`; firmware uses `accel_select_high` for Gentle Catch.

High-g scaling does not inherit low-g calibration. Offsets and transition steps
can occur. The original samples remain available for examining such effects.

## Activities have different assumptions

- **Gentle Catch** confirms 100 ms of freefall, detects contact, and reports the
  largest absolute axis during a fixed 150 ms window. It uses the FIFO but does
  not write the protected recording arrays. It currently computes neither
  integrated acceleration nor collision energy. A soft, long catch can extend
  beyond its measurement window.
- **Level and Friction** rely on gravity as a reference; motion can disturb that
  interpretation. Read the filtering, settling and trigger logic before treating
  a moving reading as an angle or coefficient.
- **Beam** uses a different rate/range and a sustained trigger. Its frequency
  estimator works on an averaged scratch signal, removes offset/drift and uses
  autocorrelation. It returns zero when periodicity is not convincing.
- **Orientation** temporarily configures sensor fusion at nominal 60 Hz and
  restores the previous registers when stopped. V1 encodes a quaternion in one
  XYZ FIFO record and reconstructs w; V2 sends XY and ZW in two records, including
  signed w. Firmware assembles those records before sending Q2 lines. Heading
  is relative and can drift. Orientation is separate from drop recording; the
  recorded drop payload contains acceleration, not gyro or quaternion data.

The viewer's flight-height estimate assumes ballistic flight with launch and
catch at approximately equal heights. CRC success establishes byte integrity,
not calibration accuracy. Neither a peak nor an acceleration-magnitude integral
alone measures collision energy.

## Read and experiment without changing measurements

First open a saved `.egg` file in the [Capture Viewer](https://dr-kohl.github.io/EggBert-Instrumented-Egg/).
Compare Low-g, High-g and Automatic, then inspect the CSV. Trace one sample from
its file bytes through `parseEgg` to its plotted value. Explain the units before
changing the arithmetic.

Other useful exercises:

- Follow the ring indices across wrap and locate the trigger in chronological
  order. Explain why the trigger sample is counted separately.
- Feed a near-threshold sequence into `accel_select_high` and predict each
  source choice before reading the test's expected result.
- Read the v1/v2 orientation fixtures in `tests/imu_driver_test.c`. Explain why
  treating a V2 half-pair as a V1 record can produce an incorrect orientation.
- Follow a button press to a state change and then to `show_ui`. Explain why a
  menu transition alone should not imply permission to erase data.

## Build and verify a change

The [README](README.md) documents the toolchain, SDK, hardware checks and test
commands. Its paths reflect the development machine; use your own installed
paths when configuring a student checkout. An existing configured build uses
`cmake --build build`. Do not flash a board until its unsaved recording has been
downloaded: rebooting is enough to lose a RAM capture.

For browser work, `node tests/dual_viewer_test.cjs` and
`node tests/orientation_test.cjs` exercise decoding, units and orientation/USB
behavior. The dual-viewer test uses the generated ARM format fixture, so follow
the README setup if it is missing in a fresh checkout. Driver and algorithm
tests use ARM builds and `tools/run_arm_test.py`, with simulated hardware where
appropriate. A passing simulation does not replace checking the physical board.

Keep changes focused and explain the result you expect to change. Use comments
for units, assumptions and reasons, not for translating each line into English.
Whether a human or AI proposes the code, review it against a concrete physical
example and the existing regression checks.

For byte layouts and orientation conventions, continue with
[EGG_FORMAT.md](EGG_FORMAT.md) and [ORIENTATION_PROTOCOL.md](ORIENTATION_PROTOCOL.md).
