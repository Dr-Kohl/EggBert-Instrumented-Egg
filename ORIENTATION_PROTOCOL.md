# Live fused orientation

The Capture Viewer requests `orientation fused` with EggBert at HOME and capture
idle. The LSM6DSV on-chip SFLP game rotation uses accelerometer and gyro at 60 Hz,
with +/-4 g and +/-2000 degrees/s. Only rotation records are batched into FIFO.
Capture retains its separate 3.84 kHz accelerometer-only FIFO.

## Serial format

```text
ORIENTATION Q1 60HZ SCALE 1000000
Q1,milliseconds_since_boot,qw,qx,qy,qz
```

Quaternion components are signed integers scaled by 1,000,000. Timestamp is a
32-bit boot millisecond counter. FIFO tag 0x13 supplies IEEE binary16 x/y/z;
w is reconstructed as the nonnegative square root of 1 minus vector norm.
Small half-precision rounding overshoots are normalized; nonfinite and grossly
invalid records are discarded. The browser normalizes again and uses shortest
path quaternion interpolation, so equivalent q/-q representations do not jump.

`orientation off`, a device button, a download request, USB CDC disconnection,
FIFO overflow, or sensor timeout stops fusion and restores the prior sensor
registers. A fresh sample is allowed before normal lab consumers resume.
The legacy `orientation on` acceleration stream remains available. Older
firmware's unknown-command response makes the viewer fall back to tilt only.

## Coordinates and recentering

Sensor and CAD axes agree: +X USB, +Y buttons, +Z outward through the screen.
Start with buttons down, screen toward you, and USB to your left. Hold still
for the first 0.6 seconds of samples. The first settled quaternion becomes the
reference. The existing X=180-degree CAD home transform H is retained. The
motion frame uses H * inverse(reference) * current * inverse(H).

Return to this home pose and select Recenter to replace the reference. Heading
is relative to that pose, not a compass bearing; this six-axis device has no
magnetometer and can exhibit slow yaw drift.

## Verification (2026-10-09)

- RP2350 ARM ELF builds and verifies on board F5865D9E680214DB.
- Initial stream: 299 quaternion records in five seconds.
- Three repeat sessions: 180/179/180 records, 59.75/59.73/59.73 Hz.
- Quaternion norms in repeat tests: 0.99999949 to 1.00000073.
- Normal +/-2 g readings return after Stop and serial close/reopen; capture
  status remains IDLE with no FIFO overrun.
- `node tests/orientation_test.cjs` checks all three axis mappings, inverted
  and full-turn poses, q/-q equivalence, invalid records, recenter, USB release,
  unplug/error cleanup, legacy fallback, and CRC decoding of all five samples.
- Physical axis directions, moving full turns, inverted tracking, and actual
  on-device capture/lab operation after orientation still require a hands-on
  check. Automated math and saved-file checks do not substitute for that check.

ST register references: [LSM6DSV driver](https://github.com/STMicroelectronics/lsm6dsv-pid)
and [SFLP example](https://github.com/STMicroelectronics/STMems_Standard_C_drivers/blob/master/lsm6dsv_STdC/examples/lsm6dsv_sensor_fusion.c).
