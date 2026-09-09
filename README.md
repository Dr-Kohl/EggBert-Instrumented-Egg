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

At boot, each LED lights for 300 ms. The OLED then shows **EGGBERT / RP2354A /
BRINGUP** and **IMU OK** if the IMU answers with `WHO_AM_I = 0x70`. Holding SW5,
SW2, or SW3 turns on green, yellow, or red respectively. Acceleration values are
reported at 4 Hz in raw counts (0.061 mg/LSB at the configured +/-2 g scale).

## Deliberate exclusions

GPIO16–21 are reserved for the LSM6DSV IMU; GPIO29 is the OUT+ voltage sense;
the QSPI pins are flash signals. This bring-up project does not drive them.
