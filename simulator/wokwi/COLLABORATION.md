# Collaboration rules

Use the [Windows setup](README.md#windows-one-command-setup) and a personal Wokwi
account/token. VS Code must already be installed. Compare the branch, source
commit, dirty state and tool versions in `.tando-install.json` when reproducing a result.

- Read repository `AGENTS.md`. Normal changes start from current `develop`; this
  shared-source simulator is reviewed on `feature/wokwi-shared-firmware`. The
  installer defaults to `develop`; the review bootstrap selects the feature
  branch explicitly. After merge, omit that override or choose `-Branch develop`.
- PR20 is closed historical work. The uploaded local ROI snapshot was preserved
  for this migration; reopening or merging the old PR is not part of this work.
- The firmware modules under `firmware/tando_final_demo_15min/` are the shared
  source. Keep simulator-specific transport/cache/input adapters build-selectable;
  preserve default board behavior, NVS, pins, UID and Stage/Care timing.
- The four known firmware issues stay separate from this migration. A visual
  Serial event test bypasses actual Touch detection and RFID latching.
- An installer rerun never switches branches or discards local edits. It builds
  a dirty selected branch without fetching/merging; a clean branch can fast-forward
  to the exact fetched commit. Use a separate path for a new review checkout when
  the old Windows clone contains uncommitted work.
- Before review, run the current renderer/pixel and project tests, installer tests
  on Windows PowerShell 5.1/current PowerShell, board and simulator builds, Wokwi
  lint and custom-chip WASM tests. Record executed, skipped and failed outcomes.
- With working Wokwi access, run the scenario/verifier and inspect both live eyes
  and all four actual Idle/PET captures. Record Blink/PET/FOOD/SLEEP and the exact
  tested build. Build success and synthetic image tests do not prove live rendering.
- For timing, keep the simulator visible and capture two complete `SIMPERF` lines
  with `bg_cache` about ten real seconds apart. Record real elapsed time separately;
  virtual `millis/micros` and model microbenchmarks are not actual GUI FPS.
- Keep credentials, `.venv`, `.pio`, generated WASM, Serial logs, screenshots and
  local installer reports out of Git. Review evidence before sharing it. CLI
  tokens and VS Code license activation remain per-user account state.
- Never rewrite existing release tags/assets. New finalized changes follow the
  repository RC policy; a feature branch is shared source, not a hosted editor URL.

## همکاری فارسی

از همان دستور نصب استفاده کن و برای این کاندید مسیر جدا انتخاب کن تا کلون قدیمی
و کارهای Commit‌نشده حفظ شوند. شبیه‌ساز از منبع ماژولار مشترک ساخته می‌شود؛
شاخه، Commit و وضعیت تغییر محلی را همراه نتیجه نگه دار. نصب‌کننده شاخه را خودکار
عوض نمی‌کند؛ بعد از ادغام می‌توانی `-Branch develop` بدهی.

برای فرمان‌ها از **Wokwi Terminal** استفاده کن. نتیجهٔ ساخت، اجرای ابری، بررسی
خودکار تصویر و مشاهدهٔ انسانی را جدا گزارش کن. برای بررسی سرعت، دو خط کامل
`SIMPERF` دارای `bg_cache` با فاصلهٔ حدود ده ثانیهٔ واقعی لازم است. فایل‌های کلید و
توکن را در Git، سند یا گفتگوی عمومی قرار نده.
