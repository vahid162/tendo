# Collaboration rules

- Make simulator changes on a feature branch and open a pull request.
- Start from develop, not a historical release; keep firmware version documented.
- Do not modify stable release tags or the authoritative product firmware to fix simulator issues.
- Keep real firmware behavior and the Wokwi-only transport adapter separate.
- Before PR, run: python tests/test_project.py, platformio run, and custom chip WASM compile.
- If possible, attach Serial logs and screenshots from BOTH round eyes after testing.
- Do not commit Wokwi credentials, API tokens, .pio output or local .venv.
- A GitHub branch is shareable source; coworkers can clone and edit independently.
