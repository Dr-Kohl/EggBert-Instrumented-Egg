# Beam recording

Select BEAM on HOME 2 and press MID ARM. Mount EggBert firmly on a clamped
beam. A 2.5-second settling delay is followed by one second of stable samples
(vector range at most 0.04 g). The resulting three-axis gravity baseline stays
fixed while armed. READY / FLICK appears when settling is complete.

A vector acceleration change of at least 0.2 g (1,639 counts at +/-4 g)
for five consecutive 480 Hz samples starts the recording. A sample below the
threshold resets this consecutive count. The trigger is independent of which
axis points up. Tilting or moving the assembly can trigger it, so students
should leave the clamp and mounting alone after READY.

The saved recording contains 7,200 XYZ samples: 240 pre-trigger samples,
the detection sample, and 6,959 subsequent samples. Total duration is 15 seconds;
the countdown after detection therefore runs for about 14.5 seconds. Samples
remain in RAM until erased or power is lost. MID STOP cancels while settling or
armed; once triggered, the recording runs to completion. Existing saved data
is protected: download and erase before starting another capture.

Acquisition uses the accelerometer FIFO at 480 Hz and +/-4 g. FIFO rate,
accelerometer rate, and range registers are checked at startup. FIFO overflow
is reported in the file; a 1.5-second sensor timeout cancels with a serial error.
Clipping near either +/-4 g rail is flagged. Ordinary drop recording remains
3,840 Hz, +/-16 g, five seconds, with its existing freefall trigger.

On completion, EggBert shows a frequency estimate over 0.3--20 Hz. It ignores
the first half-second after detection, chooses the axis with greatest variance,
averages eight samples per point to 60 Hz, removes the mean and linear trend,
and finds the earliest strong local autocorrelation peak. It reports NO CLEAR
PERIOD for weak or poorly repeating data, and suppresses the estimate after
FIFO overflow. This is a dominant vibration estimate; multiple modes, changes
in gravity from bending, clipping, and rapid decay can affect it. Verify on
the actual beam using the downloaded waveform and known cycle times.

EGG1 remains version 1 with the existing 32-byte header. The header specifies
480 Hz and 8,192 counts/g for Beam, rather than hard-coded drop settings.
Flag 0x08 denotes Beam; flag 0x10 denotes clipping. Existing flags 0x01
(triggered), 0x02 (freefall), and 0x04 (FIFO overflow) are unchanged. CRC covers
the six-byte XYZ payload as before. The Capture Viewer reads the header's rate
and scale, labels Beam flick, and avoids interpreting Beam data as freefall.

Validation: tests/beam_test.c exercises settling, arbitrary gravity direction,
short disturbances, five-sample detection, vector triggers, damped signals from
0.6 to 10 Hz, constant data, and noise. Compile it for ARM and run using
tools/run_arm_test.py (see its docstring for the command and dependencies).
tools/test_capture_viewer.cjs covers legacy captures and the Beam file header.
tools/make_beam_capture_test.py extracts the actual firmware capture functions
for a hardware-stubbed test: arm long enough to wrap the RAM ring, trigger,
then verify the pre-trigger window, sample ordering, and exactly 7,200 samples.
Link its generated C file with tests/beam_test.c using -DBEAM_CAPTURE_TEST.
