# Saturation-aware impact reporting — future revision note

## Context

EggBert's LSM6DSV accelerometer is configured for a +/-16 g per-axis range.
An impact can drive one or more axes beyond that range. The stored value then
clips near the sensor rail, so the true peak acceleration was not measured.

The capture viewer currently identifies those points as saturation. Its honest
conclusion is therefore **at least 16 g on that axis**, not a known peak value.

## Proposed optional viewer feature

Consider an **experimental peak extrapolation** in the web viewer. It would use
the unsaturated slope leading into and out of a clipped region, plus the amount
of time an axis remains clipped, to fit an assumed pulse shape.

Suggested viewer output hierarchy:

1. Primary: `>=16 g — sensor clipped`.
2. Supporting measurement: clipped axis or axes and clipped duration in ms.
3. Optional, visually secondary: `Experimental estimate: about NN g (low confidence)`.

The estimate must be opt-in or clearly subordinate to the measured lower bound.
Never replace `>=16 g` with an extrapolated value in the main result.

## Candidate model

A simple symmetric parabolic pulse can be used as a first exploratory model.
For a clipping threshold `H`, an estimated slope magnitude `s` at the threshold,
and clipped duration `D`, the model's overshoot is approximately:

```text
estimated peak - H = s * D / 4
```

The slope should be estimated from several unsaturated samples immediately
outside each side of the clipped region. Use both sides when available and flag
as lower confidence when only one edge exists.

## Limits and safeguards

- This is a model, not a measurement. A triangular pulse with the same visible
  slope and clipped duration yields a substantially different extrapolation.
- A brief peak can occur between 3,840 Hz samples, making both peak height and
  clipped duration uncertain.
- Real impacts often ring, rebound, or contain multiple pulses; a simple smooth
  pulse model may not match them.
- Apply the analysis per axis. A clipped multi-axis vector magnitude cannot be
  reconstructed honestly from the clipped components.
- If saturation is only one sample, report a very-low-confidence estimate or
  suppress the estimate entirely.
- Preserve and show the raw points at close viewer zoom so users can inspect the
  actual evidence behind any label.

## Educational value

Even without extrapolating a peak, clipped duration and the unsaturated edge
slopes can compare otherwise similar impacts. More time at the rail and steeper
edges generally indicate a more severe event. This supports lessons about
cushioning and sensor limits while keeping the distinction between measured and
inferred information clear.

## Before implementation

1. Test candidate fits on several real EggBert captures with different impact
   surfaces and cushioning.
2. Decide a minimum clipped-duration and edge-sample requirement.
3. Choose conservative wording, a low-confidence visual style, and an easy way
   to hide the estimate.
4. Keep the raw-axis saturation markers and the `>=16 g` lower-bound report as
   the authoritative result.
