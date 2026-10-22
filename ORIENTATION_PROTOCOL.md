# Live fused orientation

The Capture Viewer requests `orientation fused` with EggBert at HOME, a completed
recording, or a completed Beam result, with capture idle. Saved recordings remain
available after orientation stops. On both IMUs, on-chip SFLP game rotation uses
accelerometer and gyro at a nominal 60 Hz,
with +/-4 g and +/-2000 degrees/s. Only rotation records are batched into FIFO. Each serial record also includes live acceleration and angular rate for resting-pose validation.
Capture retains its separate 3.84 kHz accelerometer-only FIFO.

## Serial format

```text
ORIENTATION Q2 60HZ SCALE 1000000 ACCEL 8192 GYRO 70MDPS
Q2,milliseconds_since_boot,qw,qx,qy,qz,ax,ay,az,gx,gy,gz
```

Quaternion components are signed integers scaled by 1,000,000. Timestamp is a
32-bit boot millisecond counter. Acceleration uses 8192 counts/g; gyro uses 0.07 degrees/s per count. Q1 records remain readable for earlier fused firmware.
V1 LSM6DSV FIFO tag 0x13 supplies IEEE binary16 x/y/z; w is reconstructed as the
nonnegative square root of 1 minus vector norm. V2 LSM6DSV320X uses two tag 0x13
records: uint16 marker 0 followed by binary16 x/y, then uint16 marker 1 followed
by binary16 z/w. The driver joins records with matching FIFO time-slot tags and
preserves signed w. Incomplete, interrupted, and invalid pairs are discarded.
Small half-precision rounding overshoots are normalized; nonfinite and grossly
invalid records are discarded. The browser normalizes again and uses shortest
path quaternion interpolation, so equivalent q/-q representations do not jump.

`orientation off`, a device button, a download request, USB CDC disconnection,
FIFO overflow, or sensor timeout stops fusion and restores the prior sensor
registers. A fresh sample is allowed before normal lab consumers resume.
The legacy `orientation on` acceleration stream remains available. Older
firmware's unknown-command response makes the viewer fall back to tilt only.

## Coordinates and recentering

CAD axes are +X USB, +Y buttons, +Z outward through the screen. Physical measurements confirm sensor +Z for screen-up and sensor +Y for buttons-down. Thus CAD-to-sensor mapping B is a Z=180-degree rotation (X/Y reversed, Z retained).
The confirmed screen-up measurement has positive Z acceleration and a nearly
identity SFLP quaternion. SFLP maps body coordinates to a Z-up world; the
viewer maps that world to Three.js Y-up with an X=-90-degree rotation A.

Start resting in any position. Initialization waits for 0.6 seconds of stable
samples: acceleration within 0.9..1.1 g, angular rate below 4 degrees/s, small
acceleration changes, and quaternion gravity agreement within about 10 degrees.
The initial model stays hidden while waiting. Stable readings identify Screen
up/down, Button side up/down, USB side up/down, or Tilted. Moving readings do
not assert a resting side.

The existing X=180-degree CAD group H remains. Motion is yaw(heading) * A * q * B *
inverse(H), so the final CAD transform is yaw(heading) * A * q * B. Recenter changes
only heading, rotating around display +Y and preserving every side's actual
tilt. Heading faces the screen toward the camera when its horizontal projection
is defined; screen-up/down uses the USB axis instead. Recenter is available only
after stable initialization and while resting. It never forces the model home.

Heading is relative, not a compass bearing; the device has no magnetometer and
can exhibit slow yaw drift. Legacy accelerometer-only firmware still shows tilt
from gravity but does not offer gyro heading recentering.

## V2 verification (2026-10-21)

- The former single-record decoder produced 256 samples with no stable viewer
  initialization on the user-confirmed screen-up V2 board.
- The corrected decoder produced 313 samples; the actual viewer initialized
  successfully and identified Screen up. Motion and other physical resting sides
  still need a hands-on check on V2.
- Starting and stopping orientation from a completed 19,200-pair recording
  preserves both downloadable formats byte for byte, including their CRCs.
- ARM driver tests cover both IMUs, split records, signed w, mismatched slots,
  malformed quaternions, interruptions, and overflow recovery.

V2 FIFO reference: [ST AN6119, SFLP game rotation vector](https://www.st.com/resource/en/application_note/DM01076787.pdf), tables 103 and 104.

## V1 verification (2026-10-09)

- RP2350 ARM ELF builds and verifies on board F5865D9E680214DB.
- Initial stream: 299 quaternion records in five seconds.
- Three repeat sessions: 180/179/180 records, 59.75/59.73/59.73 Hz.
- Quaternion norms in repeat tests: 0.99999949 to 1.00000073.
- Normal +/-2 g readings return after Stop and serial close/reopen; capture
  status remains IDLE with no FIFO overrun.
- `node tests/orientation_test.cjs` checks all three axis mappings, inverted
  and full-turn poses, q/-q equivalence, invalid records, recenter, USB release,
  unplug/error cleanup, legacy fallback, and CRC decoding of all five samples.
- Gravity update: 298 Q2 records at 59.75 Hz from the user-confirmed screen-up pose; stable initialization and screen-up CAD direction pass on those records.
- Button-side-down hardware check: 181 records at 59.74 Hz; positive sensor Y maps to CAD buttons down and initializes stably.
- All six synthetic resting poses, face-down recentering, translation/rotation rejection, and quaternion/gravity disagreement checks pass.
- Physical moving full turns, other resting sides, and actual
  on-device capture/lab operation after orientation still require a hands-on
  check. Automated math and saved-file checks do not substitute for that check.

ST register references: [LSM6DSV driver](https://github.com/STMicroelectronics/lsm6dsv-pid)
and [SFLP example](https://github.com/STMicroelectronics/STMems_Standard_C_drivers/blob/master/lsm6dsv_STdC/examples/lsm6dsv_sensor_fusion.c).
