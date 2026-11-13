# EggBert low-power work

Agreed November 13, 2026. Shared firmware for V1 (LSM6DSV) and V2
(LSM6DSV320X). This work is intentionally staged.

## Stage one: H7-idle1

Implemented and built locally; not yet programmed or physically validated.
All 14 ARM regression suites passed, including the real V1/V2 SPI driver,
SSD1306 commands, idle policy and actual application standby/wake integration.

The test timeout is IDLE_TIMEOUT_MS = 60000u in include/idle_power.h.
After validation, change that single setting to 15u * 60u * 1000u.
Buttons, incoming serial characters and significant live acceleration changes
restart the timer. Motion compares each axis against a retained anchor, so
small successive movements accumulate; the threshold is about 0.08 g.
A stationary gravity vector and small sensor noise do not restart the timer.

At idle, stop sounds, send SSD1306 display OFF (retaining contrast and pixels),
stop app ticks/sampling/ADC reports, turn off gyro and V2 high-g, and place the
low-g sensor in 7.5 Hz low-power two-average mode. Range remains +/-2 g, just
like normal live sampling; the same sensor supports +/-16 g recording.
The controller is still running its cooperative loop (20 ms while idle).
No MCU dormant/deep-sleep instruction, reset, power-off or flash write is used.
No additional LED is enabled, and existing LED state/power-indicator wiring is
unchanged. There is still baseline board/LED/regulator current.

Any button wakes it. Consume all buttons in the waking press/chord until the
buttons are released, so waking cannot arm, erase or change a selection.
Restore the saved sensor registers, allow live data to settle, turn on the
OLED and redraw the SAME menu/result. Completed low/high capture buffers,
metadata, settings, pending calibration and app state are retained in RAM.
Incoming serial commands also wake it before normal command processing.
There is no motion wake in stage one.

## Activities that stay awake

Capture acquisition/arming, Beam waiting/listening, orientation streaming and
open USB CDC host sessions inhibit standby and restart the full idle interval.
Blocking downloads cannot be interrupted by the cooperative idle service.
Pendulum waiting/measuring, armed Friction, active calibration and Maze
centering/playing inhibit standby. The running Gyro app also inhibits standby.
Home, live Accel/Level/Raw, Settings, unarmed activity menus, paused Maze and
completed results can idle. New apps inhibit standby by default unless their
can_idle callback explicitly permits it. Powering off still loses RAM captures.

## Register references

ST official definitions for both sensors give CTRL1 (10h) operating mode 4 as
XL_LOW_POWER_2_AVG_MD and ODR 2 as ODR_AT_7Hz5: combined value 42h.
The driver checks readback, refuses FIFO/fusion sessions and never accesses
the V2-only high-g register (4Eh) on V1. Restore uses the pre-standby values.

- https://github.com/STMicroelectronics/lsm6dsv-pid/blob/master/lsm6dsv_reg.h
- https://github.com/STMicroelectronics/lsm6dsv320x-pid/blob/master/lsm6dsv320x_reg.h

## Physical test before stage two

1. Program H7-idle1; keep the battery powering the device throughout.
2. Return Home, close the serial monitor/viewer, set it down and leave it alone
   for about one minute. The OLED should switch off. An open serial monitor
   inhibits idle, even if no commands are sent.
3. Press each of the three buttons in separate trials. Each first press should
   only wake; the next press after release should behave normally.
4. Repeat from a completed recording. Compare downloaded bytes before/after
   standby, without erasing or power cycling.
5. Verify armed experiments stay awake; finish/cancel and then wait a minute.
6. Repeat on V1 and V2, and measure battery current before/after standby.
   No battery-life improvement has yet been measured or promised.

## Stage two (pending)

Add processor sleep with RAM retained, GPIO button wake, and low-power IMU
motion interrupt wake. Verify both IMUs' interrupt configuration and processor
clock/USB restoration on hardware. Preserve the stage-one capture and activity
protections. Consider production timeout only after repeated successful trials.
