# Programming EggBert boards

Use this procedure for every JLCPCB Instrumented Egg / EggBert board. It is intentionally specific: the normal workflow uploads an **ELF over the board's USB serial connection**, not a UF2 copied to an `RP2350` drive.

## Authoritative source and firmware

- Work from this checkout: `C:\Users\Kohlc\OneDrive - Cedarville University\Documents\ChatGPT\EggBert Instrumented Egg`
- Use the current local checkout after confirming `git log -1`. This checkout may be newer than the public remote.
- Build before programming:

  ```powershell
  cmake --build build
  ```

- Program this artifact: `build\eggbert_bringup.elf`

Do **not** substitute an old Arduino template, generic Blink firmware, a `.uf2`, or an unrelated RP2040 robot project. Those may appear to flash successfully but will not run EggBert's OLED, IMU, buttons, capture menu, or serial protocol.

## Standard upload procedure

1. Plug in one egg at a time.
2. Identify its current USB serial port, such as `COM25` or `COM26`. The port can change after a reboot.
3. Upload the ELF with Raspberry Pi Picotool:

   ```powershell
   & 'C:\Users\Kohlc\AppData\Local\Arduino15\packages\rp2040\tools\pqt-picotool\4.1.0-1aec55e\picotool.exe' load -v -x -f 'build\eggbert_bringup.elf' -t elf
   ```

4. Success is `Verifying Flash: ... OK`, followed by `The device was rebooted to start the application.`
5. Wait for the board to re-enumerate, then identify its new COM port if a serial diagnostic is needed.

The `-f` option tells Picotool to reboot compatible running EggBert firmware into its ROM loader. The COM port number is not an argument to Picotool: it finds the compatible USB device itself.

## If Picotool says no BOOTSEL device is accessible

For a fresh board or a board carrying non-EggBert firmware, perform the Arduino-style serial touch first. This does not require pressing BOOTSEL.

```powershell
$serial = [System.IO.Ports.SerialPort]::new('COM25', 1200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.DtrEnable = $true
try { $serial.Open(); Start-Sleep -Milliseconds 150; $serial.Close() } catch { }
Start-Sleep -Seconds 2
& 'C:\Users\Kohlc\AppData\Local\Arduino15\packages\rp2040\tools\pqt-picotool\4.1.0-1aec55e\picotool.exe' load -v -x -f 'build\eggbert_bringup.elf' -t elf
```

Replace `COM25` with the port currently assigned to that egg. A serial-open exception can still coincide with the device entering its loader; always check the Picotool result rather than assuming the touch failed.

## Post-flash checks

- The boot sequence briefly exercises all three LEDs.
- The OLED should show the portrait **HOME** menu.
- The OLED uses I2C address `0x3C` on GPIO24 (SDA) and GPIO25 (SCL). A blank screen on one board after a verified identical ELF is not a firmware mismatch. Inspect OLED power/ground, SDA/SCL solder joints, and the display module; a module at `0x3D` is a less common alternative.
- A known-good flashed board reappears as a USB serial device, though Windows may assign a different COM number after every reboot.

## Quick checklist

- [ ] Correct EggBert checkout, not a generic Arduino/RP2350 template.
- [ ] `cmake --build build` succeeds.
- [ ] Program `build\eggbert_bringup.elf` with Picotool.
- [ ] Confirm `Verifying Flash: OK`.
- [ ] Check LEDs and HOME screen.
- [ ] If needed, repeat the 1200-baud serial touch, not a UF2 drag-and-drop workflow.
