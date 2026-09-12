# Capture Storage Design

## Decision

EggBert version 1 will store complete IMU captures in **RAM only**. It will not
use LittleFS or another flash filesystem for raw capture logs.

The RP2354 provides 2 MB of in-package QSPI flash, but that flash also stores
firmware and requires erase/write management. The added complexity does not
improve the first version's central job: capture a short event and transfer its
raw evidence to a computer.

## Baseline capture capacity

The initial impact-capture configuration is:

```text
Accelerometer: 3 axes, signed int16 samples
Sample rate:   3.84 kHz
Duration:      10 seconds maximum
Capture size:  about 230 KB
```

This is appropriate for a single in-RAM capture buffer with useful remaining
memory for firmware, USB, display, and control state.

## User workflow

```text
Arm -> Capture to RAM -> Freeze buffer -> Ready to download -> USB transfer
```

After an event, the OLED should clearly report that the capture is volatile:

```text
CAPTURE COMPLETE
KEEP POWER ON
CONNECT USB TO SAVE
```

The USB host program downloads the binary capture, validates its CRC, and saves
the `.egg` file on the computer. Firmware keeps the capture in RAM until the user
explicitly clears it or confirms overwriting it with a new capture.

## Accepted tradeoff

Switching EggBert off, resetting it, or exhausting its battery before download
loses the RAM capture. This is an intentional version-1 tradeoff in favor of a
simple, robust experiment workflow.

## Why not write raw data to flash during capture

- Flash programming and sector erases complicate real-time timing.
- QSPI flash also holds the executing firmware.
- Repeated classroom tests add unnecessary write/erase wear.
- A filesystem adds metadata, recovery, and corruption-handling work.
- One complete RAM capture is already large enough for the initial use case.

## Future persistence option

Revisit persistent logging only when EggBert needs to retain data after power-off
or collect multiple experiments without a connected computer.

If that need arises, do not begin by writing to LittleFS during a live event.
Instead:

1. Capture the complete raw event in RAM.
2. After capture ends, allow the user to choose `SAVE`.
3. Write the capture to a dedicated append-only flash partition.
4. Give each record a header, sequence number, length, and CRC.
5. Recover only complete, CRC-valid records at boot.

Small persistent configuration values, such as calibration constants and UI
preferences, may later use a separate flash settings area. They do not require a
general filesystem.

## Software architecture

Keep capture acquisition independent of storage. The IMU pipeline should write
to a capture-buffer interface, allowing a future flash-backed store without
changing sensor timing or binary-format code.
