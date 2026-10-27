# CLI build and Wokwi simulation

Use the [one-command Windows setup](README.md#windows-one-command-setup) for the complete workflow. It needs preinstalled VS Code and x64 Windows 10/11 with PowerShell 5.1 or newer. Installation and builds use CLI; browser license confirmation and the final VS Code simulator command remain user actions.

## Rebuild an installed checkout

From the `simulator/wokwi` terminal, run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\install.ps1 -InstallPath (Resolve-Path ..\..).Path -NoUpdate
```

This preserves the current checkout, checks dependencies, builds both outputs, prompts for a missing token, and retries simulation. Omit `-NoUpdate` to allow a clean checkout to fast-forward. Dirty source is preserved and built without an update. `-SkipSimulation` explicitly leaves cloud and visual results unverified and returns exit code `2`; `-NoOpen` suppresses the final VS Code open request. `-ToolsRoot` changes the managed tools directory when needed.

The default checkout is `$env:USERPROFILE\Documents\Tando-Wokwi\tendo`. Managed tools default to `$env:LOCALAPPDATA\Tando-Wokwi`. The simulator's `.venv` contains PlatformIO `6.2.0` and Pillow `11.3.0`. Executable paths and versions appear in `.tando-install.json`, so CLI use does not depend on a permanent PATH change.

## Manual build commands

After an installer run has created its report, use the exact tools recorded there:

```powershell
$tandoReport = Get-Content -LiteralPath .\.tando-install.json -Raw | ConvertFrom-Json
$tandoPython = $tandoReport.Tools.PlatformIO.Path
$tandoWokwi = $tandoReport.Tools.Wokwi.Path
& $tandoPython -m platformio run
if ($LASTEXITCODE -ne 0) { throw 'Firmware build failed' }
& $tandoWokwi chip compile gc9a01.chip.c -o gc9a01.chip.wasm
if ($LASTEXITCODE -ne 0) { throw 'Custom chip build failed' }
& $tandoWokwi lint .
if ($LASTEXITCODE -ne 0) { throw 'Diagram lint failed' }
& $tandoPython -m unittest discover -s tests -p 'test_*.py' -v
if ($LASTEXITCODE -ne 0) { throw 'Simulator tests failed' }
```

These commands do not install missing tools; use the installer if a recorded tool is absent. For another operating system, create a Python virtual environment, install the pinned packages, and obtain the official Wokwi CLI from its [installation documentation](https://docs.wokwi.com/wokwi-ci/cli-installation). The checked-in `platformio.ini`, `diagram.json`, `wokwi.toml`, chip sources, and `src/main.cpp` define the build; `src/main.cpp` includes the same `sketch.ino` used by the simulator.

For model development, `node tests/test_chip.mjs` runs the compiled WASM against Wokwi API boundary stubs (Node 22+ is a development/CI tool, not an installer prerequisite). It checks normal cyan/black colors, split SPI writes, CS selection, the round mask, and independent display state. The pinned Adafruit initialization sends `INVON` for this panel's normal polarity; the model preserves normal RGB colors in that mode. These unit checks do not exercise the cloud engine.

Expected local outputs:

```text
.pio/build/tando-s3-sim/firmware.bin
.pio/build/tando-s3-sim/firmware.elf
gc9a01.chip.wasm
```

The installer checks command exit status, file existence, and binary signatures before advancing. PlatformIO decides which firmware objects need rebuilding. Do not copy a binary from another commit into a local test and call it a current-source result.

## Runtime and image verification

Rerunning the installer is the preferred secure route: it prompts for a missing `WOKWI_CLI_TOKEN` with hidden input, restores the prior environment, clears the four previous smoke images and Serial log, runs `tests/smoke.yaml`, and calls `tests/verify_simulation.py`. Do not put a token literal in a command, a saved script, or Git.

The scenario waits for the firmware-ready banner, resets simulator progress, captures both Idle eyes, sends PET, captures both PET eyes, and checks FOOD and SLEEP/WAKE events. Its reset affects this simulation's demo state. Paths are relative to this simulator directory:

```text
screenshots/left-idle.png
screenshots/right-idle.png
screenshots/left-pet.png
screenshots/right-pet.png
```

The [official scenario API](https://docs.wokwi.com/wokwi-ci/automation-scenarios) supports `wait-serial`, `write-serial`, and `take-screenshot` with `part-id` and `save-to`; the API is still alpha. Capturing this custom display depends on the actual engine's support. Missing captures are a failed or unverified visual result.

For an environment where a token is already provisioned securely, the underlying commands are:

```powershell
& $tandoWokwi . --scenario tests/smoke.yaml --timeout 30000 --serial-log-file sim-smoke.log
if ($LASTEXITCODE -ne 0) { throw 'Wokwi scenario failed; inspect its error and Serial log' }
& $tandoPython tests/verify_simulation.py --serial-log sim-smoke.log --screenshots screenshots --version $tandoReport.Firmware
if ($LASTEXITCODE -ne 0) { throw 'Serial/image evidence failed verification' }
```

Before using those manual commands, remove or archive the four generated smoke screenshots and `sim-smoke.log` so old files cannot stand in for the new run. The installer performs that cleanup for its own run. Wokwi's timeout normally returns a nonzero code; do not force timeouts to succeed to bypass a stalled scenario.

The verifier requires expected boot/event lines and rejects fatal/crash output. It checks four 240×240 PNGs, opacity, a nonblank cyan eye on a dark surround, and an Idle/PET change on each eye. It reports `visual_review: required` even after passing. These checks are smoke evidence, not reference-image matching or proof of natural animation. Review all four actual captures and both live screens before claiming visual correctness. Synthetic test fixtures validate the verifier only.

## Logs, access, and diagnostics

The installer report is `.tando-install.json`; its runtime log is `sim-smoke.log`. Exit code `0` means automated runtime/image checks passed, `2` means the workspace was prepared with simulation unverified, and `1` means a stage failed. Read the status and recovery text as well as the code.

`WOKWI_CLI_TOKEN` grants CLI cloud access. It does not activate the VS Code extension. Follow [the official license flow](https://docs.wokwi.com/vscode/getting-started) and [current plan requirements](https://wokwi.com/pricing), then run `Wokwi: Start Simulator`. With unavailable CLI access, a licensed GUI test is practical, but the missing automated runtime/image evidence stays unverified.

The current Arduino-ESP32 SPI implementation attempts to attach MISO even when the display bus requests `-1`. On ESP32-S3 HSPI, this can print `spiAttachMISO(): HSPI Does not have default pins on ESP32S3!`. The displays use SCLK/MOSI for output, so that warning alone does not establish a failed transfer. Inspect other SPI errors and actual frames. A black display or panic fails acceptance regardless of boot banners.

The demo lasts 30 minutes of Active Demo Time, starts with an interaction, and pauses after 60 seconds of inactivity. The short smoke scenario does not verify Stage transitions or the whole demo. Firmware personality, Care, Progress, and Stage rules must not be weakened to make a simulator test pass.

## GitHub Actions outputs

The installer builds locally and does not silently substitute CI artifacts. For a manual artifact download, choose a successful workflow run whose head commit exactly matches the source being tested. Authenticate to GitHub where required, inspect the run and artifact identity, and keep its commit with the evidence. Put `firmware.bin` and `firmware.elf` in `.pio/build/tando-s3-sim/` and `gc9a01.chip.wasm` beside `wokwi.toml`. Artifacts from another commit are not evidence for the current checkout. Rebuild locally if source equivalence cannot be established.

CI validates source/tests and builds outputs. It does not provision a Wokwi cloud token or certify GUI activation/rendering. See [the complete Persian guide](README.md#راهنمای-کامل-فارسی) for installation, screenshots, and recovery steps.

