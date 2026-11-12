# Pendulum measurement

The pendulum uses calibrated live X acceleration at the normal +/-2 g scale.
The hook is on the -X side, so the absolute X reading is the radial signal.
Each bottom crossing produces a peak; two crossings make one full oscillation.
The detector returns the full period in seconds. The displayed length is
`g * (period / (2*pi))^2`, in centimeters: an equivalent small-angle pendulum
length, rather than a measurement of the accelerometer's distance from the pivot.

## Detection and confidence

- Smooth samples with a 50 ms time constant, using actual elapsed time.
- Require a rise and fall around each peak. The reversal threshold follows
  10% of the last accepted peak prominence, bounded between 0.010 and 0.060 g.
- Require at least 0.025 g peak prominence. Lower-amplitude movement is not
  sufficient to establish a reliable crossing.
- Timestamp the peak itself, rather than the later confirmation of its fall.
- Ignore intervals shorter than 300 ms without resetting the accepted peak.
- Once timing is established, ignore early peaks below 80% of the median
  interval. An interval above 120% starts a fresh sequence, protecting against
  missed peaks or changed motion.
- Require six intervals whose total spread is at most 15% of their median.
  Trim the shortest and longest, average the remaining four, and double the
  result to obtain a full-cycle period.
- Show `TRY AGAIN / NO RESULT` if valid peaks stop arriving or no consistent
  result is established within 60 seconds. MID starts a new attempt; DOWN exits.

The progress display is `PASS n/6`. Six bottom-to-bottom intervals correspond to
three full cycles. The signal should be a smooth planar swing without twisting.
Use modest angles for length comparison. No amplitude or damping correction is
applied to the displayed equivalent length.

## Validation and regression coverage

The C regression suite runs the actual production detector, rather than a
separate approximation. It covers full periods of 0.8, 1.2, 1.4, 2.0 and 4.0
seconds, different starting phases, damping, noise, a missed crossing, uneven
sample timing, timestamp wraparound, and rejection of stationary, weak and
persistently irregular motion. The application integration test feeds radial
samples for a two-second physical swing through the real Pendulum app and
checks a two-second result and approximately 99.4 cm (the old bug gave 24.8 cm).
It also checks stop/rearm/home controls and weak-signal retry behavior.

Run the ARM suites with the installed compiler and the Unicorn/pyelftools test
runtime available to Python:

```text
python tools/run_arm_regressions.py --cc <path-to-arm-none-eabi-gcc>
```

Alternatively, `python tools/run_native_tests.py` runs the native detector,
application and screen regressions with `CC` set to a host compiler. Production
firmware is built with `cmake --build build`.

## Integration history

The earlier detector and this note were left as uncommitted changes in the
OneDrive checkout. The modular app retained consecutive-peak timing as a full
period. H6 integrates the corrected detector into the current app, production
build and regression runners. Both V1 and V2 use the same calibrated low-g input.

A real-board check is still needed after installing H6: time several full
back-and-forth swings with a stopwatch, compare with PER, and compare LEN with
the pivot-to-center-of-mass distance using modest angles. No amplitude or finite
bob-size correction is applied. Sensor thresholds need further recordings to
characterize the useful length, amplitude and damping range.

H6 release verification: production firmware build and all 12 ARM regression
suites passed. Actual OLED previews passed the text-overflow check; only the
intended Pendulum progress/result screens changed among existing snapshots.
The new weak-signal screen was also inspected. No board was reflashed during
this correction; physical validation remains pending.
