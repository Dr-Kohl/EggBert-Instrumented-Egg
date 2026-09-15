# EggBert RP2354A board bring-up

First firmware for the JLCPCB Instrumented Egg board. It tests the three LEDs,
three pushbuttons, LSM6DSVQTR IMU, and the 128x64 I2C OLED while printing results
over USB CDC.

## Pin assignment

| System | RP2354A GPIO | Notes |
|---|---:|---|
| Green LED | 1 | high = on (assumed; reverse if board indicates otherwise) |
| Yellow LED | 2 | high = on (assumed) |
| Red LED | 12 | high = on (assumed) |
| SW5 | 0 | internal pull-up, pressed = 0 |
| SW2 | 3 | internal pull-up, pressed = 0 |
| SW3 | 13 | internal pull-up, pressed = 0 |
| OLED SDA | 24 | I2C0, external 4.7 kOhm pull-up fitted |
| OLED SCL | 25 | I2C0, external 4.7 kOhm pull-up fitted |
| LSM6DSV MISO / SDO | 16 | SPI0 RX; U5 pin 1 |
| LSM6DSV CS | 17 | SPI0 CS; U5 pin 12 |
| LSM6DSV SCK / SPC | 18 | SPI0 SCK; U5 pin 13 |
| LSM6DSV MOSI / SDI | 19 | SPI0 TX; U5 pin 14 |
| LSM6DSV INT2 / INT1 | 20 / 21 | U5 pins 9 / 4; configured as inputs |

The OLED driver targets a 128x64 SSD1306 at `0x3C`. If the display does not
respond, rebuild with `-DOLED_I2C_ADDRESS=0x3D`.

## Build

Install the Raspberry Pi Pico SDK and an ARM embedded toolchain, then configure:

```powershell
cmake -S . -B build -DPICO_SDK_PATH="C:/path/to/pico-sdk"
cmake --build build
```

`eggbert_bringup.uf2` is produced in `build/`. Hold BOOTSEL while connecting USB,
then copy that UF2 to the RPI-RP2 drive. The project defaults to the Pico SDK
bundled with the installed Arduino-Pico core when it is present.

## Expected behavior

At boot, each LED lights for 300 ms. The OLED then shows the rotated portrait
**HOME** screen if the IMU answers with `WHO_AM_I = 0x70`. The left yellow rail
uses yellow-on-black power icons: a vertical battery gauge on battery power or a
plug when USB power is present. Its one-letter developer marker shows the current
state (`I`, `A`, `R`, `E`, or `D`). Acceleration values continue to be reported
over USB serial at 4 Hz in raw counts (0.061 mg/LSB at the live-read scale).

### On-device capture workflow

EggBert owns capture control. Use the OLED and three buttons to navigate:

```text
Home -> Record -> Drop Test -> Arm -> Hold Still -> Ready -> Capture Complete
```

The top button moves the selection up, the middle button selects it, and the
bottom button moves it down. At `CAPTURE COMPLETE`, the capture is protected in
RAM: connect the USB webpage to download it, or use `SELECT = OPTIONS` and the
two-step `ERASE CAPTURE` confirmation before rearming. A new capture cannot
overwrite a completed one accidentally.

### Gentle Catch game

`Home -> Catch -> Gentle` starts a feedback-only catching challenge. EggBert
waits for 100 ms of confirmed freefall, detects the following catch, measures
its first 0.15 seconds, and displays the peak **axis** acceleration in a
double-size font. `CLIPPED / 16G+` means an axis reached the sensor's
measurement limit, so the true impact was at least that large. The result stays
on screen until the next confirmed freefall. After EggBert is held still near
1 g for 0.5 seconds, its green LED turns on to show it is ready for the next
throw without clearing that last result. Its yellow or red outcome LED remains
on for five seconds. Press the middle button at any time to leave the game.

The LED score is solid yellow below 5 g (gentle), blinking yellow from 5 to
under 10 g (firm), and blinking red at 10 g or above or for any clipped catch.

The game never writes the RAM capture buffer, so it cannot overwrite a saved
`.egg` capture that is awaiting download or deliberate erasure.

USB CDC is for download and diagnostics, not capture arming. Type `help` in a
serial terminal to see the available commands:

| Command | Result |
|---|---|
| `arm` (or `start`) | Reports that arming must be done from the EggBert menu. |
| `stop` | Reports that capture control is restricted to the EggBert menu. |
| `status` | Reports state, stored sample count, and whether the IMU FIFO overflowed. |
| `download` | Sends the completed capture in the `EGG1` binary format. |
| `clear` | Reports that erasing must be done from the EggBert menu. |

This milestone uses the IMU FIFO, a 32-sample watermark interrupt on IMU INT1
(GPIO21), and a 115,200-byte RAM ring buffer. After one second of magnitude
between 0.8 g and 1.2 g, it triggers only on magnitude below 0.45 g (freefall).
The downloaded recording is always five seconds: 0.5 seconds before freefall
and about 4.5 seconds afterward. This removes the menu/arming delay and avoids
an accidental trigger from the throwing motion. Those first-pass thresholds are
intended to be tuned from real tests. It does not write flash. After a capture
stops, the firmware returns the IMU to its existing 120 Hz, +/-2 g live-read
configuration.

## Capture viewer web app

The static viewer is in [`docs/`](docs/), so this repository can be published
directly with GitHub Pages. It connects through Web Serial in Chrome or Edge,
downloads the completed RAM capture, verifies its CRC, and plots X/Y/Z plus
acceleration magnitude. It can also open and save raw `.egg` capture files.

The `download` USB command sends an `EGG1` version-1 binary frame: a 32-byte
little-endian header followed by chronological raw signed 16-bit X/Y/Z samples.
The header contains the sample rate, counts-per-g conversion, event marker,
post-event sample count, flags, and a payload CRC-32.

## Deliberate exclusions

GPIO16–21 are reserved for the LSM6DSV IMU; GPIO29 is the OUT+ voltage sense;
the QSPI pins are flash signals. This bring-up project does not drive them.
