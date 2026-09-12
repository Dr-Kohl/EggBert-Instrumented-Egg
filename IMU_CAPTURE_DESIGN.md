# IMU Capture and Impact-Analysis Design

## Goal

EggBert should capture short, high-resolution motion and impact events, normally
ten seconds or less. The device records **raw binary sensor data** and transfers
it to an off-board program for conversion, graphing, and analysis. The raw data
remains the authoritative experimental record.

The existing 120 Hz bring-up mode is suitable for a live display, but not for
impact capture. At 120 Hz, samples arrive 8.3 ms apart and a short impact can be
missed or represented by very few samples.

## Recommended first capture mode

```text
Sensor:       3-axis accelerometer only
Range:        +/-16 g
Sample rate:  3.84 kHz
Maximum time: 10 seconds
Sample format: signed 16-bit X, Y, Z values
Storage:      RP2354 RAM during capture
Transfer:     USB after capture
```

Storage requirement:

```text
3 axes x 2 bytes x 3,840 samples/second x 10 seconds = 230,400 bytes
```

This leaves useful RAM headroom while providing about 19 samples across a 5 ms
impact. Do not write the capture directly to flash during an event; flash writes
are slower, complicate timing, and create unnecessary wear.

## Future capture modes

| Mode | Intended use | Initial configuration |
|---|---|---|
| Impact Capture | Egg-drop and cushioning tests | Accelerometer, +/-16 g, 3.84 kHz, 10 s |
| High-Speed Impact | Shorter, faster events | Accelerometer, +/-16 g, 7.68 kHz, 5 s |
| Motion Capture | Tumble and rotation analysis | Accelerometer + gyro, 1.92 kHz, duration based on available RAM |

Enable gyroscope logging only when the experiment needs angular motion. It
doubles sample storage and SPI traffic.

## FIFO and interrupts

Use the LSM6DSV FIFO from the first capture implementation. Configure a FIFO
watermark and route its interrupt to an RP2354 interrupt pin. On each watermark
interrupt, burst-read the accumulated words over SPI into the capture buffer.

Benefits:

- Consistent sample timing without continuous host polling
- Reduced chance of missing samples
- Efficient block reads over SPI
- Ability to include IMU timestamps and tagged data when needed

The FIFO is a short-term hardware buffer, not the complete ten-second store.
The RP2354 RAM buffer holds the complete capture.

## Capture state flow

```text
Idle -> Armed -> Capturing -> Event Marked -> Capture Complete -> Download Ready
```

Suggested behavior:

1. The user arms the EggBert test.
2. Firmware starts the raw capture buffer.
3. The IMU detects or firmware identifies free fall and marks its sample index.
4. Firmware identifies impact and marks its sample index.
5. Capture continues to the requested duration or until a configured post-impact
   interval completes.
6. The device freezes the buffer, displays `READY TO DOWNLOAD`, and transfers
   data only after the event.

Later versions may use a RAM circular buffer for a defined pre-trigger period.

## Binary file format

Use a versioned binary format rather than text logging during capture. A capture
file should contain:

```text
magic number and file-format version
sample rate and configured full-scale range
sample count and sample layout / axis order
event indices: arm, free-fall, impact, end
battery voltage and relevant device configuration
optional IMU timing calibration / timestamp metadata
raw signed int16 samples
CRC or other integrity check
```

For accelerometer-only mode, each sample is six bytes:

```text
int16 x, int16 y, int16 z
```

The desktop tool converts raw counts to g, plots individual axes and vector
magnitude, finds peaks, identifies event timing, flags clipping, and compares
trials.

## Sensor range and saturation

The LSM6DSV accelerometer reaches a maximum full scale of +/-16 g. A hard impact
may exceed that limit. Firmware must detect and preserve saturation rather than
reporting the clipped value as the true peak.

Initial physical testing should measure:

- Whether the sensor reaches +/-16 g
- The typical impact duration
- Whether cushioning reduces peak acceleration below saturation
- Whether the selected sample rate resolves the impact pulse adequately

If impact tests routinely clip at +/-16 g, EggBert can still compare designs
qualitatively but cannot report exact peak acceleration. A future hardware
revision would then need a dedicated high-g accelerometer.

## Embedded IMU processing

The LSM6DSV contains useful embedded functions, including a FIFO, free-fall and
motion interrupts, a programmable finite state machine, adaptive
self-configuration, and low-power sensor fusion.

Use these features to make capture reliable and convenient, not to replace raw
logging:

- **FIFO:** Use immediately.
- **Free-fall / wake-up interrupt:** Use as an event marker or trigger.
- **Finite state machine:** Evaluate later for autonomous drop or impact state
  detection.
- **Sensor fusion / quaternions:** Add only if orientation analysis proves useful.
- **Raw data:** Always retain for impact experiments.

## Implementation order

1. Extend the IMU driver for configurable full scale, output data rate, FIFO,
   watermark, and interrupts.
2. Implement accelerometer-only raw capture at 3.84 kHz.
3. Define the binary capture header and USB download command.
4. Build a desktop parser and graphing tool.
5. Add free-fall and impact markers.
6. Run controlled physical tests and tune thresholds.
7. Add high-speed and gyroscope capture modes only after the baseline works.
