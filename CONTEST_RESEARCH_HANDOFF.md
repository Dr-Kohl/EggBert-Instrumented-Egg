# EggBert hammer-launch contest: research and development handoff

Discussion date: November 4, 2026 (America/New_York). Some supplied filenames carry November 5 dates; their names are preserved unchanged.

## Start here tomorrow

- Repository: <https://github.com/Dr-Kohl/EggBert-Instrumented-Egg>
- Continue on **`codex/gyro-arming`**, not `master`.
- H4 firmware source commit: **`073c2bb2f55ff0ec511f8c585ad84707f5f74bdd`**. Later documentation commits do not change that firmware.
- Existing draft PR: <https://github.com/Dr-Kohl/EggBert-Instrumented-Egg/pull/1>, targeting `codex/hammer-hit`.
- **H4 was successfully programmed and flash-verified on the V2 EggBert connected as COM5.** Post-reboot serial status confirmed `hammer-hit-4`, LSM6DSV320X ID `0x73`, and OLED OK.
- **No contest-specific launch/landing scoring changes have been implemented yet.** Current H4 reports the maximum over the entire five-second recording, so a landing can determine the displayed maximum.
- The separate newer `master` settings/maze/modular-menu work has not been merged into this development branch.

For a new work-PC checkout:

```powershell
git clone --branch codex/gyro-arming https://github.com/Dr-Kohl/EggBert-Instrumented-Egg.git
cd EggBert-Instrumented-Egg
```

For an existing checkout, preserve local work, inspect `git status`, then:

```powershell
git fetch origin
git switch codex/gyro-arming
git pull --ff-only
```

Read this file, [the H4 handoff](HAMMER_HIT_HANDOFF.md), and [programming instructions](PROGRAMMING_EGGBERT.md). Do not reflash merely to resume discussion; the connected egg already received H4. Download protected RAM recordings before any future power-off/reflash.

## 1. Confirmed apparatus and contest intent

These details were supplied by Clint:

| Item | Confirmed detail |
| --- | --- |
| Bare EggBert mass | **43 grams** |
| Anticipated wrapped total mass | Roughly **2 to 4 times bare mass: 86 to 172 grams** |
| Launcher | A pivoted **3-pound sledgehammer**, approximately **12-inch handle/pivot radius** |
| Location | Hammer mounted on a **30-inch-high tabletop**, launching toward the ground below |
| Objective | Make wrapped EggBert travel as far as possible **without exceeding the chosen acceleration criterion** |
| Design tradeoff | Raising the hammer increases available energy; padding must transfer useful momentum while limiting the impact pulse |
| Research intent | Ground the contest in published acceleration-duration research, including NASA human-impact tolerance curves |

The supplied drawing is a conceptual pendulum-impact schematic, not a verified drawing of the actual launcher. For this contest, wrapped EggBert must be free to depart its support after the strike; the fixed-specimen/anvil arrangement shown in the schematic is not itself the flight experiment.

The original user-supplied schematic remains in the chat/local companion files; it is not published in this repository.

## 2. Proposed rules and decisions still open

**The following are proposals, not finalized or medically validated rules.**

- Proposed total package mass cap: approximately **170 g**, counting EggBert, padding, tape, and any ballast. The anticipated 172 g at exactly four times bare mass is an estimate, not an approved allowance.
- Proposed short-impact design level: **40 g**. An earlier proposal used a maximum 10-ms moving average. Once the hammer-launch setup was clarified, we identified that **10-ms averaging alone could hide a short, high spike**. The final measurement/filter/window and any peak limit remain undecided.
- Proposed distance measure: distance to **first ground contact**, excluding rolling and bouncing.
- Proposed qualification: evaluate the **launch pulse separately from landing**. Keep landing data for analysis, but do not let it disqualify an otherwise acceptable launch unless the contest deliberately adds landing protection as a separate objective.
- Define a common maximum package size, starting position, release method, launch geometry, hammer release range, and trial/retry policy before issuing rules.
- Decide whether launch angle is fixed or is itself an allowed design variable.
- Use labels such as **target met / target exceeded / measurement invalid or clipped**, rather than safe / fatal.

The earlier 15/20 g bands were a different illustrative cushioning target loosely informed by roadside ridedown criteria. They were not adopted. The current discussion is about piloting a 40 g launch target and examining the full short-duration waveform.

### Why weight does not supply a human-scaling factor

For identical speeds and acceleration/stopping distances, acceleration does not depend on mass. More mass increases energy and changes how a given cushion deforms; it can improve material loading or cause the cushion to compress fully. Do **not** multiply allowed g or pulse duration by two or four simply because wrapping increases mass.

NASA scale-model crash research uses specified geometric, material, mass, and dynamic scaling procedures. EggBert is a rigid instrumented payload, not a geometrically or biomechanically validated miniature person. Its measurement is useful for comparing protective designs, but cannot establish human survival or injury probability.

Sources: [NASA scale-model/full-scale crash comparison](https://ntrs.nasa.gov/citations/20030065638); [Canadian Conservation Institute cushioning glossary](https://app.pch.gc.ca/application/padcad/glossaire-glossary.app?lang=en).

## 3. NASA acceleration-duration research

The likely curve remembered from a student's NASA work is the **Eiband curve**:

- A. Martin Eiband, **Human Tolerance to Rapidly Applied Accelerations: A Summary of the Literature**.
- NASA Memorandum **5-19-59E**, June 1959.
- [NASA catalog record](https://ntrs.nasa.gov/citations/19980228043).
- [Original report PDF](https://ntrs.nasa.gov/api/citations/19980228043/downloads/19980228043.pdf).
- **Figure 2: printed page 58, PDF page 62**, for spineward loading. [Open at the figure](https://ntrs.nasa.gov/api/citations/19980228043/downloads/19980228043.pdf#page=62).

View the original chart directly in the [public NASA report, PDF page 62](https://ntrs.nasa.gov/api/citations/19980228043/downloads/19980228043.pdf#page=62).

The figure uses logarithmic axes: uniform acceleration in g versus duration in seconds. It distinguishes volunteer exposures without injury/debilitation, moderate injury, and severe injury, under the described extensive body-support conditions. Its evidence includes humans, animals, and accident observations.

The text describes the following historical voluntary exposures for the specific loading/restraint conditions:

| Approximately uniform acceleration | Duration |
| --- | --- |
| **45 g** | **44 ms** |
| **40 g** | **100 ms** |
| **25 g** | **200 ms** |
| **13 g** | **600 ms** |
| **10 g** | **1.6 s** |

These are **not universal human-safe limits**. The report explicitly states that a fatal-injury boundary could not be established. Restraint, body direction, onset rate, pulse shape, local head/chest motion, and individual characteristics matter.

### Follow-up reference: representing the whole pulse

[FAA AC 21-22, Injury Criteria for Human Exposure to Impact](https://www.faa.gov/documentLibrary/media/Advisory_Circular/AC_21-22.pdf), especially printed page 4 and Figures 1-4, discusses these charts and their limitations. It describes representing an irregular pulse by exposure duration at multiple acceleration levels, rather than assigning one peak-duration point. It also cautions that the resulting curve still does not address all injury factors or velocity change.

For EggBert, comparing a measured exposure curve with a clearly identified historical reference could be an educational research comparison. A lower curve or a contest target must not be presented as a validated human-survival classifier.

Head-specific criteria such as HIC combine acceleration and duration, but require appropriate head measurements and assumptions; simply calculating a HIC-like number from a freely wrapped rigid device would not validate it as a human-head surrogate. [NHTSA reference](https://www.nhtsa.gov/sites/nhtsa.dot.gov/files/fmvss/AdvAbRul_1.pdf).

## 4. Hammer energy and the launch tradeoff

Approximation: treat the 3 lb hammer head as a point mass of 1.361 kg, with its center 12 in (0.3048 m) from the pivot. Neglect handle mass, pivot friction, and other losses. Measure release angle from the vertically downward resting position:

```text
Head height above bottom = L * (1 - cos(theta))
Available head energy = M * g * L * (1 - cos(theta))
Head speed at bottom = sqrt(2 * g * L * (1 - cos(theta)))
```

| Release angle | Available head energy | Approximate head speed at bottom |
| --- | --- | --- |
| **30 degrees** | **0.54 J** | **0.89 m/s** |
| **45 degrees** | **1.19 J** | **1.32 m/s** |
| **60 degrees** | **2.03 J** | **1.73 m/s** |
| **90 degrees: handle horizontal** | **4.07 J** | **2.45 m/s** |

Confirm the actual pivot-to-head-center distance and whether 3 lb refers to the head or entire hammer. These are available hammer energies, **not energies guaranteed to enter EggBert**. Collision behavior, deformation, rebound, and the handle affect transfer.

The launch principle is impulse:

```text
Integral of net launch-direction acceleration over time = change in launch velocity
Impulse = integral of contact force over time
```

This requires the appropriate direction and gravity treatment; integrating raw acceleration magnitude is not generally an accurate launch-velocity measurement if the egg rotates.

For an illustrative horizontal velocity gain of **4 m/s**:

| Acceleration pulse | Approximate contact/acceleration time needed |
| --- | --- |
| Constant **40 g** net launch-direction acceleration | **10.2 ms** minimum |
| Half-sine pulse with **40 g peak** | **16.0 ms** |

These calculations show why a shaped, longer pulse can deliver useful launch velocity under a peak constraint. Very dissipative padding may lower launch velocity by wasting energy; overly stiff contact can create a brief high peak. Material stiffness changes impact pulse duration and shape: [Endevco explanation](https://www.endevco.com/our-resources/ask-the-experts/selecting-the-correct-modal-hammer).

## 5. Thirty-inch launch height: example ranges

Assume the center of EggBert leaves **horizontally from 30 in (0.762 m)** above the landing surface, with no initial vertical velocity and negligible air resistance:

```text
Flight time = sqrt(2 * h / g) = approximately 0.394 s
Horizontal range = horizontal launch speed * flight time
```

| Horizontal launch speed | Calculated distance to first ground contact |
| --- | --- |
| **2 m/s** | **2.6 ft** |
| **3 m/s** | **3.9 ft** |
| **4 m/s** | **5.2 ft** |
| **5 m/s** | **6.5 ft** |

These are examples, not predictions of the actual launcher. Upward launch angle, package drag, and different center-of-mass heights change the result. Just falling 30 in produces approximately **3.9 m/s downward speed** before ground contact, so landing may generate a much larger acceleration than launch.

## 6. Uploaded foam-wrapped throw/catch recording

Source filename: `eggbert-capture-20261105-024350.egg`. The raw recording is retained locally/in the chat and is not published in this public repository. Reattach it from the work PC if needed for further analysis.

Clint described a foam-wrapped egg tossed approximately **5 ft upward above release**, then caught. The file does not encode a firmware build identifier.

| Recording property | Result |
| --- | --- |
| Format | **EGG2**, paired low/high accelerometers |
| Samples | **19,200 per sensor** |
| Nominal sampling rate | **3,840 Hz** |
| Recording length | **5 seconds** |
| Trigger index | **1,920**, retaining 0.5 s of preceding samples |
| Integrity | Header CRC, payload CRC, and length all pass |
| Flags | Triggered/freefall/low-g clipping; no overflow, invalid-pair, or high-g clipping flag |
| Calibration metadata | Identity low-g calibration in this file |

| Measured result | Value |
| --- | --- |
| Peak during throw, automatic trace | **Approximately 5.0 g** |
| Peak during catch, high-g/automatic trace | **22.46 g** |
| Largest high-g individual axis, entire file | **19.73 g** |
| Highest approximately 1-ms moving average, automatic magnitude | **22.32 g** |
| Highest approximately 5-ms moving average | **21.77 g** |
| Highest approximately 10-ms moving average | **21.25 g** |
| Highest approximately 20-ms moving average | **17.61 g** |
| Highest approximately 30-ms moving average | **15.24 g** |
| Highest approximately 50-ms moving average | **10.37 g** |
| Total time above 20 g in catch, high-g magnitude | **11.2 ms** |
| Main uninterrupted catch interval above 5 g, high-g magnitude | **30.5 ms** |

Moving-average windows were rounded to integer sample counts. Values use nominal sensor sensitivities and are estimates, not calibrated impact certification. The two sensors show similar broad catch pulses. Low-g axes clip during part of the catch; the high-g sensor does not clip. The combined/vector magnitude can exceed a per-axis range.

The derived analysis chart remains with the local companion files. The numeric summary above is included in these discussion notes; no raw capture or derived chart is published here.

The catch has two distinct pulses followed by vibration; it is not merely a single-sample outlier. Possible contributors include a firm catch, fully compressed padding, changing contact geometry, or internal motion. The file alone cannot establish which occurred or how much improvement foam provided.

The approximate airborne interval is 0.9 s. With equal release/catch heights and ballistic motion, that would imply around 3.3 ft of rise. The hand-estimated 5 ft, catch-height differences, and rotation/detection effects prevent treating that as an independent height measurement.

## 7. What 43 grams tells us

For approximate rigid translation, force on the bare instrumented egg is mass times measured proper acceleration. The sensor measures non-gravitational specific force; do not add gravity again when making this force estimate.

| Event | Acceleration | Approximate force on **43 g EggBert** |
| --- | --- | --- |
| Throw | **5 g** | **2.1 N / 0.47 lbf** |
| Catch peak | **22.5 g** | **9.5 N / 2.1 lbf** |

This assumes the measured acceleration represents motion of the whole bare egg, with modest rotational/internal-motion contributions. It is **not necessarily the hammer force or total wrapped-package force**. Multiplying by package mass is appropriate only if that package shares the measured motion.

Energy examples for an object that rises above release and returns to the same height:

| Total moving mass | Energy corresponding to a 5 ft rise | Energy corresponding to a 10 ft rise |
| --- | --- | --- |
| **43 g** | **0.64 J** | **1.29 J** |
| **86 g** | **1.29 J** | **2.57 J** |
| **172 g** | **2.57 J** | **5.14 J** |

## 8. Earlier throw/catch physics examples

For a vertical toss, the heights here mean **rise above the release point**, followed by catching at approximately the release height. Ignore drag.

| Rise above release | Return speed at release height | Total airborne time |
| --- | --- | --- |
| **5 ft** | **5.47 m/s / 12.2 mph** | **1.12 s** |
| **10 ft** | **7.73 m/s / 17.3 mph** | **1.58 s** |

The average net stopping deceleration follows `a/g = fall height / stopping distance` for these idealized equal-height returns. Stopping distance includes both padding compression and movement of the catching hands.

| Effective stopping distance | Average net deceleration after 5 ft rise | Average net deceleration after 10 ft rise |
| --- | --- | --- |
| **1 in** | **60 g** | **120 g** |
| **2 in** | **30 g** | **60 g** |
| **4 in** | **15 g** | **30 g** |
| **6 in** | **10 g** | **20 g** |
| **12 in** | **5 g** | **10 g** |

These are averages, not peaks. For a straight upward supporting force during a vertical catch, the accelerometer reads approximately another 1 g above net upward deceleration. A rounded pulse generally has a larger peak than its average.

For a 10 ft upward toss produced by a 12 in upward hand stroke starting from rest, average net acceleration during the stroke is **10 g**, while the supporting accelerometer reading is approximately **11 g**. Peak can be higher. During free flight, an ideal nonrotating accelerometer reads near zero even though gravity accelerates its trajectory.

Another duration example: constant **20 g** over **10 ms** changes speed by about **4.4 mph**; over **100 ms**, about **44 mph**. This illustrates the importance of the full pulse, not a human injury prediction.

## 9. Cushioning discussion

Suggested first material to test: thick, soft **open-cell polyurethane upholstery/protective-case foam**, with around 2 in on each side as an initial comparison, without tightly compressing it with tape. This was a material-trial suggestion, not a finalized contest size rule or a guarantee of achieving the g target.

Both overly firm foam with little usable travel and overly soft foam that compresses fully can perform poorly. Density is not the same as firmness. For launch, the goal includes momentum transfer, so the best catch cushion may not be the best launch cushion.

Sources: [Polyurethane Foam Association packaging discussion](https://pfa.org/packaging-case-study/); [foam properties](https://pfa.org/foam-performance/).

## 10. Firmware state and work to consider next

### H4 already implemented and flashed

- Three-toss arming was too difficult and was replaced by **turn once, turn back, relaxed pause**.
- Gyro detects the turn/reversal. The first turn accepts any axis/sign; the return reverses around the same general axis.
- Each turn threshold is **270 degrees**, allowing an approximate full turn; each leg has a six-second window.
- Pause accumulates two seconds of supported, slow movement: up to **45 deg/s** and acceleration magnitude **0.65-1.4 g**, without holding one fixed orientation.
- Bumps up to **350 ms** preserve pause progress. Longer movement restarts only the pause; the final stage expires after eight seconds.
- A chirp confirms each turn; rising melody and steady green confirm armed. Reminder tick once per second; two-minute armed timeout.
- Gyro is separate from the paired acceleration FIFO; it is not saved in the `.egg` payload.
- Launch confirmation requires an impact followed by **200 ms of near-weightlessness**. Spin or off-center acceleration can interfere with this detection; it still needs hammer trials.
- Recordings retain 0.5 s before the candidate launch and 4.5 s including/after it, with protected RAM storage and normal EGG1/EGG2 downloads.

### Validation actually completed

- Six native suites passed: gesture, actual recording/lifecycle functions, IMU driver including gyro/FIFO coexistence, sound, scheduled OLED, and Beam recording.
- ARM firmware built with Pico SDK **2.3.1** and xPack ARM GCC **15.2.1-1.1**.
- ELF text **96,036 bytes**, static BSS **239,668 bytes**, including unchanged **230,400-byte paired recording arrays**.
- H4 ELF SHA-256: `5ba804379bcc19a44cf7bc0a71aa989c9116f5fdda077198d6b3460202b09c81`.
- Flash verification passed for device serial **29DA76A03DEC6A11**; it restarted on **COM5** with H4, sensor, and OLED confirmed.
- This does not establish physical hammer-launch accuracy, arming reliability through wrapping, or the final contest rule.

### Priority next steps

1. Record several low-energy hammer launches from known release angles, preserving the raw files.
2. Examine strike duration, high-g clipping, paired-sensor agreement, sensor bandwidth/sample coverage, double contact, spin, and whether launch detection succeeds.
3. Identify launch and landing separately. Current `summarize_hammer()` scans all samples and therefore needs revision for launch-only contest scoring.
4. Finalize the scoring window/filter/exposure curve and how clipped, incomplete, or unresolved narrow pulses are handled. **At 3,840 Hz, one millisecond contains only about four samples.** A sub-millisecond spike may not be characterized reliably.
5. Keep raw data and show launch peak, selected short-window average, pulse duration/exposure curve, and landing peak separately.
6. Finalize mass/size/release/distance/trial rules after pilot results. No automatic contest pass/fail firmware changes have been implemented.

Illustration of the averaging concern: an idealized **200 g rectangular pulse lasting 1 ms** averages only **20 g over 10 ms**. A 10-ms average alone could therefore qualify a pulse whose brief peak is much larger than the intended design level.

The next development task should be driven by the actual hammer recordings rather than assuming the earlier hand-toss/catch waveform represents a hammer strike.
