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
