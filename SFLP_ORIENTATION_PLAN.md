# SFLP Quaternion Orientation Plan

## Purpose

Improve the GitHub Pages 3D EggBert viewer after the current lab and
presentation work are complete. The current viewer derives pitch and roll from
accelerometer gravity data. The next version should use the LSM6DSV's embedded
SFLP (sensor fusion low power) game-rotation quaternion for smoother motion.

## Established coordinate convention

CAD and sensor coordinates intentionally match:

| Positive axis | Physical direction |
|---|---|
| +X | USB-port side |
| +Y | Button side (under the buttons) |
| +Z | Out through the screen face |

The viewer's home pose is +Y down: buttons down, screen toward the viewer, and
the USB visually left. Preserve this mapping when applying any quaternion.

## Current state

- The LSM6DSV is currently used as an accelerometer at 120 Hz for live data.
- The existing Web Serial orientation mode sends `O,x,y,z` acceleration counts.
- The web page calculates pitch and roll from gravity.
- Capture mode has a separate 3.84 kHz accelerometer-only FIFO path.
- The current FIFO decoder assumes each FIFO record is an accelerometer record.

## Proposed architecture

1. Add a separate enhanced orientation mode; do not change normal live reads,
   capture, calibration, inclinometer, friction, pendulum, or beam behavior.
2. Enable accelerometer, gyroscope, and the LSM6DSV SFLP game-rotation vector
   only while this enhanced orientation mode is active.
3. Read tagged FIFO records for the SFLP quaternion, gravity vector, and gyro
   bias as needed. SFLP output is FIFO-only.
4. Send a versioned serial record, for example:

   ```text
   Q,qw,qx,qy,qz
   ```

   Use fixed-point integers or clearly documented scaled values; do not send
   locale-dependent decimal strings.
5. The webpage should apply the normalized quaternion directly to the Three.js
   model, after the fixed CAD-to-sensor transform. Keep the old `O,x,y,z`
   stream as a compatibility fallback until the quaternion path is verified.
6. Initialize/zero the orientation session while EggBert is in the established
   +Y-down home pose.

## Why use SFLP

The LSM6DSV's on-chip SFLP engine combines accelerometer and gyroscope data to
produce a game-rotation quaternion, gravity vector, and estimated gyro bias.
It avoids starting with a custom RP2354 fusion filter and produces a more
natural visualization during quick motion than accelerometer-only tilt.

## Limitation: yaw

LSM6DSV is a 6-axis IMU; EggBert has no magnetometer in this design. The game
rotation vector can track yaw relative to the session start, but it cannot
provide an absolute compass heading and yaw may drift over time. This is
acceptable for the visualization if each session starts from the known home
pose. An external magnetometer would be a future requirement for absolute,
long-term yaw.

## Implementation and test checklist

- [ ] Add gyroscope register configuration and read support to `lsm6dsv.c`.
- [ ] Add tagged FIFO parsing instead of assuming accelerometer-only records.
- [ ] Configure and enable SFLP only in enhanced orientation mode.
- [ ] Define/document quaternion scale and serial record format.
- [ ] Extend the web serial reader to parse and normalize `Q` records.
- [ ] Apply quaternion through the existing CAD/sensor coordinate convention.
- [ ] Verify the +Y-down home pose remains correct.
- [ ] Test pitch, roll, and a slow yaw rotation; document expected yaw drift.
- [ ] Confirm Stop still disables the stream and releases USB.
- [ ] Confirm capture and all existing labs still behave exactly as before.

## Scope guard

Do not begin this implementation until the current ECE presentation and lab
materials are ready. This is a post-presentation feature, not a prerequisite
for today's session.
