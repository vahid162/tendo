# CLI Build and Wokwi simulation

Operate through CLI to avoid interfering with the active Windows desktop.

1. Prepare Python virtual environment and PlatformIO:

    python -m venv .venv
    .venv\Scripts\python.exe -m pip install platformio

2. In the simulator/wokwi directory, compile (same sketch.ino source):

    platformio run

3. Install official Wokwi CLI if needed. Compile the custom GC9A01:

    wokwi-cli chip compile gc9a01.chip.c -o gc9a01.chip.wasm

4. To run cloud simulator, set a PRIVATE WOKWI_CLI_TOKEN environment variable
   from the Wokwi CI dashboard (not in Git, code or docs):

    wokwi-cli . --timeout 30000 --serial-log-file sim.log
    wokwi-cli . --interactive --timeout 0

5. Static tests (do not need Wokwi token):

    python tests/test_project.py

The local simulator expects platformio.ini, diagram.json, and wokwi.toml.
The Wokwi chip model expects a WASM compiled from gc9a01.chip.c.

If the screen is black, check Serial logs and the custom chip C SPI trace.
Check that both parts are in diagram.json and every control pin matches the table in README.md.

The 30-minute Active-Time demo starts with a user action and pauses after
60 seconds of inactivity (same as the imported development firmware).

## Download prebuilt outputs from GitHub Actions

Open the Actions run linked from the simulator pull request.
Download the artifacts tando-esp32s3-firmware and tando-gc9a01-wasm.
Extract firmware.bin and firmware.elf to simulator/wokwi/.pio/build/tando-s3-sim.
Extract gc9a01.chip.wasm into simulator/wokwi beside wokwi.toml.

You can then run Wokwi CLI if your account has a valid WOKWI_CLI_TOKEN.

## Dual-eye visual screenshot smoke test
After setting WOKWI_CLI_TOKEN in the same PowerShell session, run:
  wokwi-cli . --scenario tests/smoke.yaml --timeout 15000 --serial-log-file sim-smoke.log
The scenario captures four screenshots (left/right Idle and left/right PET) in the screenshots/ directory.
If serial output is empty or no PNG is generated, the simulation is not yet verified.

