# EggBert low-power work

Agreed November 13, 2026. Shared firmware for V1 (LSM6DSV) and V2
(LSM6DSV320X). This work is intentionally staged.

## Current timeout: five minutes (H8-sleep5)

Dr. Kohl confirmed shake wake and recording retention on V2, then selected
five minutes of inactivity for normal use. IDLE_TIMEOUT_MS is now
5u * 60u * 1000u (300000 ms) in include/idle_power.h. Processor sleep, wake
behavior, recording retention and reminder LEDs are unchanged. The one-minute
and proposed fifteen-minute settings below describe the earlier test plan.
For current physical checks, wait five minutes with the serial monitor closed.

H8-sleep5 built and passed all 15 ARM regression suites. Programmed and
flash-verified on V2 serial 612C09B133DDB22A; post-boot serial status confirmed
300000 ms timeout, OLED OK and live acceleration. Before reboot, the completed
19200-sample dual recording was saved under recordings/2026-11-13-before-five-minute-sleep
as .egg and .egd2; length/CRC and identical dual raw streams were validated.

## Stage one: H7-idle2

H7-idle1 and H7-idle2 were programmed and flash-verified on a V2 board. Dr. Kohl
confirmed OLED standby, button wake, motion resetting the idle timeout and
reported that the follow-up recording reminder behavior was working.
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
During standby, a completed nonempty recording is indicated by a 100 ms red
pulse every 5 seconds, starting at standby entry. Without a saved recording,
all three software-controlled status LEDs are off. Normal awake LED behavior
is restored immediately on waking. No charging/power-indicator circuitry is
changed. There is still baseline board/LED/regulator current.

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

1. Program H7-idle2; keep the battery powering the device throughout.
2. Return Home, close the serial monitor/viewer, set it down and leave it alone
   for about one minute. The OLED should switch off. An open serial monitor
   inhibits idle, even if no commands are sent.
3. Press each of the three buttons in separate trials. Each first press should
   only wake; the next press after release should behave normally.
4. Repeat from a completed recording. Compare downloaded bytes before/after
   standby, without erasing or power cycling.
5. Verify armed experiments stay awake; finish/cancel and then wait a minute.
6. Check the red standby reminder with a recording: a brief pulse every five
   seconds. Wake and confirm the normal solid red indication returns. Repeat
   without a recording and confirm all status LEDs stay off during standby.
7. Repeat on V1 and V2, and measure battery current before/after standby.
   No battery-life improvement has yet been measured or promised.

## Stage two: H8-sleep

November 13: all 15 ARM regression suites and the RP2350 firmware build passed.
Programmed and flash-verified on V2 serial 612C09B133DDB22A (COM46). After
closing the CDC session for over one minute, status confirmed one sleep entry
and wake mask 4 (USB), with OLED OK and normal live acceleration restored.
Physical shake/button wake, recording-reminder timing, V1 hardware and battery
current measurements remain hands-on checks; no current saving is quantified.

Keep the one-minute test timeout and all stage-one activity protections. Use
Cortex-M33 SLEEPDEEP plus WFI between interrupt events, retaining all SRAM and
the capture buffers. Gate unused ADC, I2C, SPI, PWM, PIO and SHA engine clocks
while asleep; restore the exact saved sleep masks and SCR before interrupts
run. Leave PLLs, flash, SRAM, GPIO, ticks, timers and USB available. This is
processor sleep, not full oscillator-stopped dormant mode or power-off. Keeping
USB available allows a host to connect without losing the recording. USB SDK
background IRQs may briefly wake the CPU; the display stays asleep. No battery
current reduction has yet been measured.

Wake inputs use a raw GPIO IRQ handler, leaving the driver's capture INT1
callback intact. Any button, low-g motion on INT2, or a connected CDC host wakes
the same view. The first button press/chord is consumed even when a brief press
is released before the main loop runs. Motion/USB wake does not issue app input.
Timer-only wakes service the 100 ms / five-second recording reminder and return
to processor sleep without turning on the OLED. With no recording, a five-second
safety timer keeps the loop recoverable without display/sensor polling.

Both IMUs save and restore every changed low-g, rate and interrupt register:
INT2_CTRL, CTRL4, CTRL8, FUNCTIONS_ENABLE, INACTIVITY_DUR, TAP_CFG0, WAKE_UP_THS,
WAKE_UP_DUR and MD2_CFG, plus CTRL1/2 and V2 high-g control. Idle uses +/-2 g,
7.5 Hz LP two-average mode, slope filtering (rejecting static gravity), threshold
weight 2 (31.25 mg/LSB), threshold 6 (187.5 mg), one-sample duration, latched
interrupts and settling mask. A 500 ms settling interval precedes clearing the
latch and routing motion to INT2. Register readback failure rolls back entry;
restoration failure leaves standby retryable. Do not change to 15 minutes until
physical wake testing succeeds on both boards.

Serial status reports sleep-entry count and last wake reason bits (button=1,
motion=2, USB=4), so timer wakes can be distinguished from a full display wake.

References:
- https://github.com/raspberrypi/pico-extras/blob/master/src/rp2_common/pico_sleep/sleep.c
- https://www.st.com/resource/en/datasheet/lsm6dsv320x.pdf
- https://github.com/STMicroelectronics/STMems_Standard_C_drivers/blob/master/lsm6dsv_STdC/examples/lsm6dsv_wakeup.c

Physical checks: leave Home stationary for one minute with the serial monitor
closed; shake to wake; repeat with each button, and confirm the first press only
wakes. Repeat with a saved recording and compare downloads across sleep/wake;
check that red reminder pulses do not wake the display. Reconnect USB after
standby and confirm status/downloads still work. Repeat on V1 before declaring
both boards physically validated. Measure battery current separately.

The actual firmware LED logic is included in the standby integration tests.
Tests cover pulse boundaries, repeat timing, clock rollover, empty/no recording,
return to awake indication, no OLED redraw during pulses, and unchanged capture
buffers/metadata through standby and wake. Stage-two tests additionally cover
both IMUs' complete wake-register configuration, rollback and restoration,
timer/button/motion/USB wake paths, an IRQ arriving with interrupts masked,
clock/SCR restoration before ISR execution, already-asserted wake inputs,
alarm-allocation failure, and input consumption through application wake.
