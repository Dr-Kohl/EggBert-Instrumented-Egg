# Sample EggBert captures

These files are synthetic `EGG1` captures generated from the documented EggBert binary format. They do not come from a physical sensor. They are useful for practicing the viewer while EggBert is unavailable.

Open the [EggBert Capture Viewer](../) in Chrome or Edge, choose **Open .egg file**, and select one of these files:

- `01-still-tilted.egg` — gravity appears across two axes even though the egg is still; magnitude remains near 1 g.
- `02-freefall-impact.egg` — a still period, near-zero freefall, and a sharp impact followed by settling.
- `03-cushioned-bounce.egg` — a lower peak with repeated rebounds; compare its shape with the sharp impact.
- `04-sensor-saturation.egg` — the simulated impact exceeds the configured ±16 g range and is clipped.

The companion `.txt` files describe the teaching point for each capture. All four files use a 1,920 Hz sample rate and 2,048 counts per g, matching the planned ±16 g capture configuration. Their CRCs are valid, so the viewer should report **CRC verified**.

The current viewer plots X, Y, Z, and vector magnitude. It does not yet calculate a separate dynamic-acceleration curve, classify events, or show a freefall interval independently from the trigger line; those are good next visualization features.
