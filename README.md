# EggBert RP2354A board bring-up

First firmware for the JLCPCB Instrumented Egg board. It tests the three LEDs,
three pushbuttons, and the 128x64 I2C OLED while printing results over USB CDC.

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
BRINGUP**. Holding SW5, SW2, or SW3 turns on green, yellow, or red respectively,
and each transition is logged over USB serial.

## Deliberate exclusions

GPIO16–21 are reserved for the LSM6DSV IMU; GPIO29 is the OUT+ voltage sense;
the QSPI pins are flash signals. This bring-up project does not drive them.
