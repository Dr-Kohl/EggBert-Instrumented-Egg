# Capture Workflow, Event Detection, and Desktop Tool Design

## Capture principle

EggBert captures raw IMU data in a continuously updated RAM ring buffer while
armed. It waits for a meaningful motion sequence rather than treating the first
handling bump as an experiment.

The event detector uses acceleration-vector magnitude rather than any single
axis:

```text
|a| = sqrt(ax^2 + ay^2 + az^2)
```

At rest, the magnitude should be near 1 g regardless of orientation. During
freefall it approaches 0 g. During an impact it rises well above 1 g.

Use squared magnitude in time-critical firmware when convenient to avoid an
unnecessary square-root operation:

```text
|a|^2 = ax^2 + ay^2 + az^2
```

Raw sensor samples remain unchanged in the capture file; vector magnitude is
only an event-detection and analysis value.

## Physical axis convention

Use the following initial orientation references:

| Axis | Approximate 1 g reference position |
|---|---|
| Z | EggBert lying flat |
| X | EggBert resting on the USB-port side |
| Y | EggBert resting on the button side |

Verify exact signs during a six-position gravity calibration test. Document the
final positive directions in the desktop tool and file format.

## Capture state machine

```text
Idle -> Armed -> Wait for Stillness -> Ready / Ring Buffering
     -> Freefall Detected or Impact Detected
     -> Post-Impact Recording -> Capture Complete -> Download Ready
```

### Arm and stillness

1. The user selects `DROP TEST` and presses Arm.
2. EggBert starts filling the RAM ring buffer with raw samples.
3. It ignores setup bumps and handling while waiting for a stable condition.
4. After approximately 0.5 seconds near 1 g, the display changes to `READY`.
5. The ring buffer continues running and preserves a defined pre-event window.

### Event detection

Initial thresholds are deliberately provisional and must be tuned with real
tests:

```text
Freefall candidate: |a| below approximately 0.3-0.4 g for a short duration
Impact candidate:   |a| above approximately 2-3 g
```

The detector supports both anticipated experiments:

```text
Drop test:       still -> freefall -> impact -> post-impact capture
Test-rig impact: still -> sudden impact -> post-impact capture
```

The first implementation should use raw-vector magnitude in firmware as the
primary trigger. The LSM6DSV freefall interrupt may later serve as a secondary
event marker or cross-check, but it must not replace the raw evidence.

### Capture windows

Keep roughly 0.5-1.0 seconds of pre-event data in the ring buffer. Once impact
is detected, retain the pre-event data and continue recording for a configurable
post-impact interval, initially 2-4 seconds, before freezing the capture.

## Calibration and threshold tuning

Do not treat the initial trigger thresholds as grading criteria. First capture
real examples of:

- Normal placement and handling bumps
- EggBert held still
- Freefall
- Intended test-rig impacts
- Hard impacts that cause sensor saturation

Use those captures to set stillness, freefall, impact, debounce, and post-event
duration settings. Keep all settings in the binary capture header.

## Failure behavior

| Condition | Device response |
|---|---|
| IMU absent | Fatal error. Show `IMU ERROR`, light red LED, and do not allow arming. |
| OLED absent | Continue capture if the IMU works; signal the failure through LEDs and USB serial. |
| FIFO overflow | Mark the capture invalid and show `DATA LOST`. |
| USB disconnect during download | Preserve the RAM capture and allow a later retry. |
| Low battery during capture | Complete the active capture when stable; clearly warn afterward. |
| Sensor saturation | Preserve raw clipped data and show `SENSOR SATURATED`; do not report a false peak. |

## Desktop analysis tool

Build a public, static GitHub Pages web application rather than requiring a
locally installed desktop program.

The intended student workflow is:

```text
Open EggBert web page in Chrome or Edge
-> click Connect EggBert
-> select EggBert USB CDC serial port
-> download capture
-> inspect graphs
-> save or import an .egg binary file
```

The site uses the browser Web Serial API to communicate with EggBert's USB CDC
port. The site must be served through HTTPS; GitHub Pages satisfies that need.
Because Web Serial support is not universal, the site must prominently tell
users to use Chrome or Edge and provide `.egg` file import as a fallback.

### Initial website capabilities

- Connect / disconnect device
- Request capture metadata and binary data
- Display transfer progress and integrity status
- Plot X, Y, Z, and vector magnitude over time
- Show impact, freefall, and saturation markers
- Save raw `.egg` files
- Import a previously saved `.egg` file

## USB protocol direction

Use a small, versioned protocol. Human-readable commands are appropriate for
control, but binary capture transfer must use framed chunks with explicit length
and CRC. Suspend ordinary debug printing while a binary transfer is active.

Suggested control commands:

```text
STATUS
GET_CAPTURE
CLEAR_CAPTURE
CONFIG
```

## Implementation order

1. Implement RAM ring buffering and raw sample capture.
2. Add stillness detection and visible `READY` state.
3. Add provisional freefall and impact markers.
4. Add binary transfer with retry and CRC.
5. Build the static browser tool and graph imported `.egg` files first.
6. Add Web Serial connection and direct download.
7. Tune thresholds from controlled physical tests.
