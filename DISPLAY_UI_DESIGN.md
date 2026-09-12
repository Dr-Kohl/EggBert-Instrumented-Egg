# OLED Orientation and User Interface Design

## Physical orientation

The 0.96-inch 128x64 OLED has a fixed color split:

- Physical top 16 pixel rows: yellow
- Remaining 48 pixel rows: blue

The OLED should be mounted and rendered **90 degrees counterclockwise** from its
current landscape orientation. The button bank lies on the edge opposite the
yellow band. After counterclockwise rotation, the yellow band becomes a vertical
rail at the left side of the display and the three buttons run vertically along
the right side.

## Button convention

From the physical top of the button bank to the bottom:

| Button position | Default action |
|---|---|
| Top | Up / previous item |
| Middle | Enter / select / confirm |
| Bottom | Down / next item |

Use short presses for normal navigation. Reserve long-press behavior only where
it has a clear, discoverable purpose, such as returning home or arming a test.

## Portrait UI geometry

The software UI treats the rotated panel as a logical **64 wide x 128 tall**
display. The fixed physical yellow band becomes a 16-pixel-wide vertical yellow
status rail. The remaining 48 pixels of width form the blue content region.

```text
logical portrait view

+--+--------------------------------+
|Y | RECORD                         |
|Y | > DROP TEST                    |
|Y |   RESULTS                      |
|Y |   SETTINGS                     |
|Y |                                |
|Y | ENTER = SELECT                 |
+--+--------------------------------+
 yellow rail        blue content
```

The narrow blue region makes concise labels important. Prefer labels such as
`RECORD`, `RESULTS`, `SETUP`, `READY`, `USB`, and `3.82V`; avoid paragraphs and
wide landscape-style headers. Use an inverted row to show the selected menu item.

## Yellow status rail

Use the yellow rail for compact, persistent state information:

- Battery icon and bars while operating on battery
- Plug icon while USB power is present
- Recording indicator
- Warning or impact indicator
- Optional page/state marker

The rail should supplement the blue content area rather than compete with it.

## Battery and USB behavior

- **Battery operation:** Show the battery icon/bars and measured voltage in the
  OLED UI.
- **USB present:** Show a plug icon and `USB POWER` or `CHARGER CONNECTED`.
- **Charging status:** Do not show `CHARGING` or `FULL` on the OLED yet. The
  battery ADC detects USB presence but cannot distinguish active charging from
  charge completion. The TP4056 charge-status LEDs remain authoritative.

See [BATTERY_ADC_DESIGN.md](BATTERY_ADC_DESIGN.md) for the power-path and ADC
design details.

## Firmware approach

The SSD1306 controller supports horizontal and vertical flips but does not
natively support a 90-degree display rotation. Implement the rotation in the
EggBert graphics layer.

Keep the existing physical 128x64 framebuffer and I2C transmission code. Add
logical portrait drawing functions that map a 64x128 coordinate system into the
physical framebuffer. For the intended counterclockwise rotation:

```text
logical coordinate:  (x, y), where 0 <= x < 64 and 0 <= y < 128
physical coordinate: x_physical = 127 - y
                     y_physical = x
```

Verify the transform on hardware with an orientation test screen before building
the full UI; a simple test should visibly label the top, middle, and bottom
button positions.

Recommended graphics API additions:

```text
ui_draw_pixel(x, y)
ui_draw_char(x, y, character)
ui_draw_text(x, y, text)
ui_fill_rect(x, y, width, height)
ui_draw_icon(x, y, icon)
```

## Performance expectations

The physical OLED framebuffer is only 1 KB. Rotating UI drawing in software is
trivial for the RP2354. Full-screen I2C transfer time, rather than CPU time, is
the limiting factor. Refresh menus and status screens only when input or state
changes, or at roughly 5-10 Hz for live data. Keep IMU sampling and other device
tasks nonblocking.

## Implementation sequence

1. Add the rotated pixel transform.
2. Add rotated character and text drawing.
3. Build and verify the orientation test screen.
4. Add the yellow status rail, button highlights, and menu selection rendering.
5. Add the battery/USB status display.
6. Integrate the screens into the main EggBert state machine.
