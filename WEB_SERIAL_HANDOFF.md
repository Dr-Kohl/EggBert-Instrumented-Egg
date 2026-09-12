# Web Serial Handoff — 2026-09-12

## What is working on hardware

- The RP2354 firmware is flashed and visible as **COM24**.
- Event capture works: a controlled three-inch drop triggered `FREEFALL`.
- A successful test reported `38400/38400` ring samples and no FIFO overrun.
- The OLED states work: `CAPTURE READY`, `HOLD STILL`, `WAIT EVENT`,
  event state, then `CAPTURE COMPLETE`.
- Firmware recognizes: `arm` / `start`, `stop`, `status`, `download`, `clear`,
  and `help`.

## New serial download protocol

Firmware implements `download` for a completed capture:

- Sends the text line `DOWNLOAD ... BINARY FOLLOWS`, followed by binary magic
  `EGG1`.
- Format version 1: 32-byte little-endian header, then chronological signed
  16-bit raw X/Y/Z samples (six bytes/sample).
- Header includes sample rate, sample count, trigger offset, post-trigger
  sample count, counts-per-g, flags, and payload CRC-32.
- RAM captures are cleared by every firmware flash/reset. A fresh physical
  capture is therefore required before a real download test.

## Viewer and repository

- Public repository: https://github.com/Dr-Kohl/EggBert-Instrumented-Egg
- GitHub Pages viewer: https://dr-kohl.github.io/EggBert-Instrumented-Egg/
- Viewer source: `docs/index.html`, `docs/app.js`, `docs/style.css`.
- Pages deploys from `master/docs`.
- The viewer supports Web Serial, `.egg` import, `.egg` save, CRC validation,
  and X/Y/Z plus magnitude plots.

## Unfinished / observed issue

The browser initially connected to COM24 and showed **Requesting capture...**
without a result. The most likely immediate cause was that the latest firmware
flash had cleared RAM, so `download` replied with its text `ERROR:` message
instead of an `EGG1` frame. The original viewer ignored text errors and waited.

The viewer has since been updated and pushed to:

- show device `ERROR:` responses instead of waiting forever;
- cancel after a ten-second no-response timeout;
- include **Disconnect** to release COM24; and
- include **Arm EggBert**, so the browser can own COM24 for the complete
  connect -> arm -> physical test -> download workflow.

These fixes were pushed as commits `780fa9e` and `ec52cef`, but the complete
browser-to-device binary download has **not yet been proven end-to-end** on a
fresh capture.

## Resume procedure

1. Wait for GitHub Pages to deploy the latest viewer, then hard-reload it.
2. Ensure no other serial terminal has COM24 open.
3. In Chrome or Edge, click **Connect EggBert**, select COM24, then click
   **Arm EggBert**.
4. Wait for OLED `WAIT EVENT`; run a controlled drop/impact.
5. Wait for OLED `CAPTURE COMPLETE`; click **Download capture**.
6. Expected result: summary cards populate, CRC reports verified, and plots
   appear. Save the resulting `.egg` file.
7. If it fails, record the exact viewer status/error and run the firmware
   `status` command only after clicking **Disconnect** in the browser.

## Important port ownership rule

Exactly one program may own COM24 at a time. The browser owns it after
**Connect EggBert**. Codex command scripts open it only briefly and close it
immediately. Do not leave a separate serial-monitor terminal connected while
using the webpage.
