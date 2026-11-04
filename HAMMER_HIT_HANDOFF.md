# Continue Hammer Hit from home

Saved November 4, 2026. Development branch: **`codex/hammer-hit`**.
The working board was programmed with **`hammer-hit-3`**, also labeled
**BUILD H3** on the Hammer Hit start screen. Do not use an older local copy
or assume `master` contains this branch.

At publishing time, remote `master` had advanced separately to **`9bc433d`**
("Add persistent sound and brightness settings", November 4). Those changes
are not combined into this Hammer Hit branch. This branch preserves the exact
H3 source tested and flashed here; fetch/review both histories before combining
them. Do not overwrite either branch to resolve the divergence.

## Get the source

For a new checkout:

```powershell
git clone --branch codex/hammer-hit https://github.com/Dr-Kohl/EggBert-Instrumented-Egg.git
cd EggBert-Instrumented-Egg
```

For an existing clean checkout:

```powershell
git fetch origin
git switch codex/hammer-hit
git pull --ff-only
```

Preserve any uncommitted home-PC work before switching branches. No prior
local development folders, recordings, or build caches are needed by this source.

## Current behavior and decisions

- HOME 2 > HAMMER HIT > MID BEGIN starts the listener before wrapping the egg.
- Three toss/catch/pause sequences arm it; an approximately one-foot toss is
  the intended starting gesture. Each accepted catch chirps after settling.
- Gentle catches are accepted, and there is no upper-g rejection on arming
  catches. Brief flight noise is tolerated. Detailed thresholds are in README.
- The display identifies HOLD STILL, TOSS NOW, IN AIR, CATCH WAIT, measured air
  time in milliseconds, and any rejection reason.
- After the third settled catch, hold still for two more seconds. A rising
  melody and steady green indicate armed; it ticks once per second.
- Armed timeout: two minutes, then repeat the gesture. Middle cancels before
  a confirmed event, but cannot interrupt a confirmed recording.
- An impact followed by 200 ms of actual near-weightlessness confirms launch.
- Retain five seconds at 3840 Hz: 0.5 seconds before the launch candidate and
  4.5 seconds including/after it. Confirmation delay does not move the window.
- Recording is the objective. H3 displays RECORDING COMPLETE, a single MAX g,
  USB DL and MID OPT. **No 16 g judgment and no HIT/END classification.**
- Preserve available samples and mark clipping/data faults. V2 stores both
  low/high raw streams in EGG2; V1 uses EGG1 and flags clipping.
- Recordings stay in RAM and are protected until explicit erase. Download
  before power-off or reflashing. Source control does not save board RAM.

## What was verified

- Firmware built successfully with Arduino-Pico 5.6.0's Pico SDK/ARM toolchain,
  CMake and Ninja. Static RAM use was approximately 235 KiB, including the
  unchanged 225 KiB dual recording arrays.
- ARM simulations passed gesture rejection, soft catches with roughly 500 ms
  noisy flight, final settling, two-minute timeout and 200 ms launch confirmation.
- Tests extracted from actual recording functions passed both versions,
  long circular waits, delayed confirmation, 19,200 retained samples, trigger
  offset 1920, high-g maximum selection and V1 clipping.
- Sound and existing Beam recording regression tests passed.
- Display scheduling tests passed: one OLED page is sent per main-loop turn
  during Hammer Hit, letting acquisition run between transfers. The first
  implementation overflowed the FIFO by drawing the whole screen synchronously.
- H3 flash verified on version 2 device serial **29DA76A03DEC6A11**, last COM41.
  Startup reported LSM6DSV320X, ID 0x73, OLED OK. A six-second live listener
  check processed over 21,000 pairs with zero sequence/tag/I/O errors and no
  FIFO overflow (maximum FIFO level 69). The test was cancelled, leaving it
  unarmed with no stored recording.

## Next physical checks

Physical three-toss arming and a complete hammer/flight/landing recording
still need validation, particularly with foam wrapping. Do not assume the
simulated gestures prove real-world reliability. Start with gentle one-foot
tosses; watch the step, air time and rejection reason if the count stays at zero.
Then verify the armed melody/tick, a complete recording, download and plotting.
Spin/off-center acceleration can interfere with near-weightlessness detection.

## Build on the home PC

Install/use CMake, Ninja, the Pico SDK and an appropriate ARM compiler.
`CMakeLists.txt` contains work-PC fallback paths. Override both paths for the
home PC rather than copying a build directory:

```powershell
cmake -S . -B build -G Ninja -DPICO_NO_PICOTOOL=1 "-DPICO_SDK_PATH=C:/path/to/pico-sdk" "-DPICO_TOOLCHAIN_PATH=C:/path/to/arm-toolchain/bin"
cmake --build build
```

Replace the two example paths. If `PICO_SDK_PATH` is set in the environment,
that value also participates in configuration; make it match the intended SDK.
Program **`build/eggbert_bringup.elf`** using the Picotool procedure in
`PROGRAMMING_EGGBERT.md`, not an unrelated UF2. The COM number can change.

For ARM simulation tests, install Python packages `unicorn` and `pyelftools`.
See the existing README ARM compile examples and `tools/run_arm_test.py`.
The new tests are `tests/hammer_trigger_test.c`,
`tests/ssd1306_schedule_test.c`, and the generated harness from
`python tools/make_hammer_capture_test.py`. The latter writes to `build/`.

Primary implementation: `src/main.c`, `include/hammer_trigger.h`,
`src/sound.c`, `include/sound.h`, `src/ssd1306.c`, `include/ssd1306.h`.
The browser under `docs/` was not changed for Hammer Hit; existing EGG1/EGG2
download and graph support is reused.
