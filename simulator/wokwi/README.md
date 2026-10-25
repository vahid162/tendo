# Tando dual-eye Wokwi simulator

Baseline: Tando develop firmware v0.10.0-rc.8 (30 minutes, three stages).
This simulator folder is isolated; production firmware in the repository root is unchanged.

## Files
- sketch.ino: real Tando firmware copied at the baseline with a simulation-only hardware bypass
- diagram.json: ESP32-S3-N8R2, two independent custom GC9A01 eyes, reaction LED
- gc9a01.chip.c and gc9a01.chip.json: custom GC9A01 SPI emulator and 240x240 framebuffer
- libraries.txt: required libraries for Wokwi online
- src/main.cpp and platformio.ini: build from same sketch.ino using PlatformIO
- wokwi.toml: run with Wokwi CLI / VS Code Wokwi extension
- tests/test_project.py: static checks of pinmap, structure, and simulation flags

## Hardware pin map
Eye SCLK=GPIO4 and MOSI=GPIO5 are shared. Left DC=6 CS=7 RST=15.
Right DC=18 CS=16 RST=17. Both use 3.3V and GND. LED=GPIO21.

## Serial commands, 115200 baud
p PET; 1 FOOD1; 2 FOOD2; s SLEEP / WAKE; u unknown RFID;
l LOOK; w WINK; e EYE SMILE; g PLAY INVITE; h hunger preview;
r pet request preview; b BLINK; i STATUS; D reset progress; ? help.

No MPR121 or RC522 physical devices are present in this simulator.
The Serial commands call the same event handlers as the actual sensors.

## Build / Test
See BUILD.md for local CLI commands, requirements and known limits.
See COLLABORATION.md for team workflow.

## Important limitations
- The custom GC9A01 emulator is a separate model. Compile success is not proof of correct visual output.
- Simulated SPI clock is 12MHz and refresh period is 120ms to reduce emulator workload.
- Software simulation does not replace on-device tests.
- Publishing on GitHub gives the team shared source, not automatically a hosted Wokwi project URL.
- This project does NOT contain a Wokwi CLI token. Each colleague must use their own token.
