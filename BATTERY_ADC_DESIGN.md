# Battery ADC and USB-Power Behavior

## Purpose

EggBert uses GPIO29 / ADC3 to monitor `Out+`, the switched system-input rail.
The measurement supports two user-facing functions:

- Show a battery fuel gauge while EggBert runs from its battery.
- Detect that USB power is present and show a plug / external-power indicator.

## Power-path behavior

The TP4056 charges the battery at `B+` from USB +5 V. R27 is 3.3 kOhm, which
sets an approximate 363 mA charge current. A 500 mAh cell therefore charges at
about 0.73 C; the selected battery must support that rate.

The Q1 P-channel MOSFET and D2 Schottky diode separate system power from the
charging path:

- **USB absent:** R20 pulls Q1's gate low, Q1 conducts, and the battery powers
  `Vsys`, then `Out+` through SW4.
- **USB present:** D2 powers `Vsys` from USB. USB +5 V drives Q1's gate high,
  isolating the battery from the system load. The TP4056 can then charge the
  battery without the system load affecting charge termination.

## ADC divider

The divider uses two 100 kOhm resistors and a 100 nF capacitor:

```text
Out+ -- 100 kOhm -- GPIO29 / ADC3 -- 100 kOhm -- GND
                                  |
                                100 nF
                                  |
                                 GND
```

The voltage relationship is:

```text
Vadc = Vout+ / 2
Vout+ = 2 x Vadc
```

At 5.0 V on `Out+`, the ADC sees 2.50 V and the divider consumes 25 microamps.
The 100 nF capacitor filters noise; with the divider's 50 kOhm Thevenin
resistance, its time constant is approximately 5 ms.

## Off-state safety

The divider must remain connected to `Out+`, not directly to `B+`.

When SW4 turns EggBert off, `Out+`, the 3.3 V regulator, and the RP2354A are
unpowered together. This prevents intentional divider drain and prevents a
battery-connected divider from applying voltage to an unpowered ADC pin.

Do not move the divider's upper resistor to `B+` without adding a default-off
measurement switch that isolates both the divider and ADC input while the
3.3 V rail is off.

## Firmware interpretation

### Battery operation

With no USB present, `Out+` closely approximates battery voltage, less the
small Q1 MOSFET drop. Use a filtered ADC reading to display a battery icon and
voltage. A first implementation can use:

```text
Vbattery approximately equals 2 x Vadc + VQ1_drop
```

At EggBert's expected load, `VQ1_drop` should be small; verify and calibrate it
with a multimeter if needed.

### USB detection

With USB present, `Out+` is supplied through D2 and should be above the maximum
battery-powered voltage. Firmware can use a threshold with hysteresis after
measuring the actual board under load. Starting values:

```text
set USB-present state:   Out+ > 4.4 V
clear USB-present state: Out+ < 4.3 V
```

Bench-test these values with a fully charged battery, a USB supply at its low
allowed voltage, and normal system load before treating them as final.

## Display behavior

| Power condition | OLED indication |
|---|---|
| Battery only | Battery icon/bars and measured voltage |
| USB present | Plug icon and `USB POWER` or `CHARGER CONNECTED` |
| Low battery | Yellow warning and `CHARGE SOON` |
| Critical battery | Flashing red warning and `CHARGE NOW`; do not begin a new capture |

The ADC determines whether USB power is present. It does not distinguish active
charging from charge complete. The TP4056's `CHRG#` and `STDBY#` signals already
drive the charge-status LEDs. A future revision can route those status signals
to the MCU with suitable off-state isolation if the OLED must show exact
`CHARGING` and `FULL` states.
