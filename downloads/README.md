# Windows batch programmer download

Download **EggBert-H8.2-Windows.zip**, extract ALL files, then open the extracted
folder and double-click **Start EggBert Programmer.cmd**. No Python installation
is required. The matching `.sha256` file verifies the ZIP download.

The package contains the Windows programmer, Python/Tcl/Tk runtime, Picotool and
its DLL/notices, the fixed H8.2 firmware and checksum, source and instructions.
It operates locally without network access. Keep the `_internal` and `picotool`
folders alongside the EXE.

Click Start, connect ONE running EggBert, wait for green verified success,
unplug, and connect the next. Unknown/blank/BOOTSEL-only devices are not
automatically programmed. Read the included instructions for recovery and logs.

The source, build instructions and limitations are in
[tools/batch_programmer](../tools/batch_programmer/README.md).
Hardware acceptance on the school PC remains pending: verify Settings shows
FW H8.2 on the first board, then confirm a second plug-in cycle before batching.
