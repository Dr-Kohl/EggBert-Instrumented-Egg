# EggBert recording formats

All fields are little-endian. `.egg` files use EGG1 for a single calibrated
acceleration stream and EGG2 for paired raw low/high acceleration streams.
Never interpret a file solely by its extension. Save preserves the original
bytes. Gyroscope samples are not part of either recording format.

## Common header fields

| Offset | Type | Meaning |
|---:|---|---|
| 0 | 4 bytes | ASCII EGG1 or EGG2 |
| 4 | u8 | Version: 1 or 2, matching the magic |
| 5 | u8 | Flags, below |
| 6 | u16 | Header size: 32 (EGG1), 80 (EGG2) |
| 8 | u32 | Nominal samples per second, per stream |
| 12 | u32 | Sample count (paired time slots in EGG2) |
| 16 | u32 | Zero-based trigger index; 0xFFFFFFFF when absent |
| 20 | u32 | Samples after trigger |
| 24 | u16 | Low-g counts per g; EGG2 uses 2048 |
| 26 | u16 | Payload stride: 6 (EGG1), 12 (EGG2) |
| 28 | u32 | CRC-32 of payload only |

EGG1 payload records are signed int16 XYZ, calibrated by the firmware before
storage. Divide counts by the header's counts-per-g value. Existing drop files
use 3840 Hz, 19,200 samples and +/-16 g; Beam uses 480 Hz, 7200 samples and +/-4 g.

## EGG2 extension

| Offset | Type | Meaning |
|---:|---|---|
| 32 | u32 | High-g micro-g per count: 10417 |
| 36 | u16 | WHO_AM_I: 0x73 (LSM6DSV320X) |
| 38 | u16 | High-g nominal range: 320 g per axis |
| 40 | 3 x i32 | Low-g offsets in live-read counts (16384 counts/g) |
| 52 | 3 x u32 | Low-g scale corrections in Q16, one per axis |
| 64 | u32 | Low-g calibration generation; 0 for identity calibration |
| 68 | u32 | Live-read counts/g used by stored offsets: 16384 |
| 72 | u32 | Reserved, must be zero |
| 76 | u32 | CRC-32 of header bytes 0 through 75 |

Each EGG2 payload record has six signed int16 values: raw low X/Y/Z, then raw
high X/Y/Z from the same FIFO time slot. Raw values are never overwritten by
calibration. Sample `i` has nominal time `i / sampleRate`; a sample counter
does not prove identical analog filter delay between accelerometers.

To reproduce on-device low-g correction per axis:

```text
offset = trunc(offsetLive * countsPerG / liveCountsPerG)
correctedCount = clamp(floor(((raw - offset) * scaleQ16 + 32768) / 65536), -32768, 32767)
low_g = correctedCount / countsPerG
high_g = rawHigh * highMicroGPerCount / 1,000,000
```

Identity calibration uses offsets 0 and scales 65536. Metadata is snapshotted
when recording begins, so recalibrating later cannot reinterpret an older
capture. High-g uses nominal factory sensitivity and has independent offsets
and noise; do not apply the low-g calibration to it. These nominal rates and
sensitivities are not a precision timebase or a device calibration.

## Flags and integrity

Flags: bit 0 triggered, bit 1 freefall, bit 2 FIFO overflow, bit 3 Beam, bit 4
low-g clipped, bit 5 invalid paired capture (EGG2), bit 6 high-g clipped (EGG2).
Clipping is checked per axis from raw counts, near the range limit. In EGG2,
absolute raw low-g >=32563 and raw high-g >=30413 are marked near clipping.
Low-g clipping alone does not mean the high-g channel clipped.

CRCs use reflected CRC-32/ISO-HDLC, polynomial 0xEDB88320, initial value
0xFFFFFFFF and final inversion (compatible with Python zlib.crc32). A valid
payload CRC proves transport integrity, not that FIFO sampling was complete.
Overflow/invalid flags must remain visible even when CRCs pass. Header CRC
failure rejects EGG2 because range/calibration metadata cannot be trusted.

## Engineering EGD2

`download raw` retains the earlier engineering-only 32-byte EGD2 frame described
in README. It is not a normal `.egg` recording and is not accepted by the
website. Its payload is identical to the corresponding EGG2 raw payload; the
headers and metadata differ. Use it for validation, not as a substitute for
the calibrated/multi-sensor recording format.

## Automatic display (no file-format change)

The viewer derives an automatic XYZ vector from EGG2: switch to high-g when any
raw or calibrated low-g axis is at least 12 g, and return only when all axes on
both representations of low-g and on high-g are below 8 g. All axes switch
together. Clipping follows the selected sensor. Both original streams remain
available; low-g alone drives flight estimates. Sensor offsets/filter delays
are not corrected by automatic selection. CSV appends the combined vector,
magnitude, low/high source and selected clipping flag after the original
18 dual-stream columns. Saved binary bytes remain unchanged.
