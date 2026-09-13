"""Generate educational EggBert EGG1 captures without hardware."""
from __future__ import annotations

import math
import struct
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / "docs" / "sample-data"
RATE = 1920
COUNTS_PER_G = 2048  # +/-16 g LSM6DSV setting
HEADER = 32


def crc32(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
    return (~crc) & 0xFFFFFFFF


def capture(name: str, samples: list[tuple[float, float, float]], flags: int, trigger: int, post: int, note: str) -> None:
    raw = bytearray()
    for x, y, z in samples:
        raw += struct.pack("<hhh", *(max(-32768, min(32767, round(axis * COUNTS_PER_G))) for axis in (x, y, z)))
    header = struct.pack("<4sBBHIIIIHHI", b"EGG1", 1, flags, HEADER, RATE, len(samples), trigger, post, COUNTS_PER_G, 6, crc32(raw))
    path = OUT / f"{name}.egg"
    path.write_bytes(header + raw)
    (OUT / f"{name}.txt").write_text(note + "\n", encoding="utf-8")


def smoothstep(t: float) -> float:
    return t * t * (3 - 2 * t)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    n = RATE * 3

    # Gravity is visible even when the egg is motionless. This capture is tilted.
    still = [(0.707, 0.0, 0.707 + 0.004 * math.sin(i / 37)) for i in range(n)]
    capture("01-still-tilted", still, 0, 0xFFFFFFFF, 0, "Still and tilted: each axis is nonzero, while magnitude remains near 1 g.")

    # Stillness -> freefall -> sharp impact -> settling vibration.
    event = []
    freefall = int(1.2 * RATE)
    impact = int(1.7 * RATE)
    for i in range(n):
        if i < freefall:
            event.append((0.0, 0.0, 1.0 + 0.012 * math.sin(i / 11)))
        elif i < impact:
            event.append((0.025 * math.sin(i / 7), 0.02 * math.cos(i / 9), 0.02))
        else:
            t = (i - impact) / RATE
            pulse = 11.0 * math.exp(-t * 85.0)
            ring = 1.2 * math.sin(t * 80) * math.exp(-t * 3.5)
            event.append((0.25 * ring, 0.15 * ring, 1.0 + pulse + ring))
    capture("02-freefall-impact", event, 3, impact, n - impact, "Freefall approaches 0 g, then impact rises sharply before settling toward 1 g.")

    # A lower, cushioned impact with several visible rebounds.
    bounce = []
    impact2 = int(1.1 * RATE)
    for i in range(n):
        if i < impact2:
            bounce.append((0.0, 0.0, 1.0))
        else:
            t = (i - impact2) / RATE
            pulse = 5.5 * math.exp(-t * 3.2) * abs(math.sin(t * 29))
            bounce.append((0.1 * math.sin(t * 18), 0.08 * math.cos(t * 23), 1.0 + pulse))
    capture("03-cushioned-bounce", bounce, 1, impact2, n - impact2, "Cushioned impact: lower peak with repeated rebounds; compare with the sharp impact capture.")

    # Deliberately exceeds the configured range; stored values clip at +/-16 g.
    saturated = []
    impact3 = int(1.0 * RATE)
    for i in range(n):
        if i < impact3:
            saturated.append((0.0, 0.0, 1.0))
        else:
            t = (i - impact3) / RATE
            saturated.append((0.0, 0.0, min(16.0, 19.0 * math.exp(-t * 75.0) + 1.0)))
    capture("04-sensor-saturation", saturated, 1, impact3, n - impact3, "Impact exceeds the +/-16 g range; the displayed peak is clipped and is not the true peak.")


if __name__ == "__main__":
    main()
