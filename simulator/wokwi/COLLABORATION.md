# Collaboration rules

Each contributor starts with the same [Windows installation command](README.md#windows-one-command-setup) and uses a personal Wokwi account/token. Only VS Code must already be installed. Compare the source commit, dirty state, tool versions, and outcomes in `.tando-install.json` when reproducing another developer's result.

- Read repository `AGENTS.md` before editing. `develop` is the current pre-release source; `main` is stable. The simulator baseline is `v0.10.0-rc.8` from `develop` commit `f181cc2285b06f70d2cce2113d30d698a2c98ac8`.
- Installer work stays on `feature/wokwi-dual-gc9a01` and updates existing [Draft PR #20](https://github.com/vahid162/tendo/pull/20). Do not merge it until the required review and real visual validation have happened. Do not change release tags or historical Releases.
- Keep simulator transport/model changes separate from product personality, Stage, Care, Progress, and physical sensor behavior. Fix simulator problems inside `simulator/wokwi` whenever the evidence supports that scope.
- An installer rerun preserves dirty source and skips its Git update. A clean checkout can fast-forward. The installer does not discard local edits or rewrite divergent history. Use `-NoUpdate` to build the current source explicitly; resolve Git conflicts after reviewing and saving the work.
- Use a new working branch for unrelated contributions. The installer expects the simulator branch; build another branch through the [manual commands](BUILD.md#manual-build-commands) or use a separate simulator checkout.
- Before review, run Python structural/verifier tests, installer tests on Windows PowerShell 5.1/current PowerShell, the PlatformIO firmware build, Wokwi diagram lint, and custom chip WASM compilation. Record which commands actually ran and their results; skipped cloud tests are not passing cloud tests.
- With valid cloud access, run the scenario and verifier. After reviewing the content, voluntarily share the Serial log and both left/right Idle and PET captures with the exact source commit in the PR. Inspect the images and live Blink/PET/FOOD/SLEEP behavior before claiming graphical acceptance. Boot banners, compilation, and synthetic image fixtures do not prove that both real simulated eyes render correctly.
- Do not commit credentials, `.venv`, `.pio`, generated WASM, Serial logs, screenshots, or the local installer report. Review logs before sharing. CLI tokens and VS Code license activation remain per-user account state.
- A GitHub branch shares source. It does not automatically create a hosted Wokwi editor project URL.

When reporting an issue, include the failed stage, recovery message, source commit, local modifications, tool versions, and redacted evidence. State separately whether the failure concerns build, authorization, boot, Serial events, screenshot capture, pixel checks, or actual rendering.

## همکاری فارسی

برنامه‌نویس دوم [همان دستور نصب](README.md#windows-one-command-setup) را اجرا می‌کند و توکن خودش را در ترمینال وارد می‌کند. برای بازتولید یک نتیجه، Commit و وضعیت تغییرات محلی دو نفر باید مشخص باشد. لاگ و تصویر را فقط پس از بازبینی محرمانگی به‌صورت داوطلبانه با همکاران به اشتراک بگذارید.

تا وقتی هر دو چشم در اجرای واقعی دیده نشده‌اند، وضعیت گرافیکی را «تأیید نشده» نگه دارید. نتیجهٔ تست ساخت، اجرای ابری، بررسی خودکار پیکسل و مشاهدهٔ انسانی را جداگانه ثبت کنید. [راهنمای کامل فارسی](README.md#راهنمای-کامل-فارسی) مسیر نصب، فعال‌سازی افزونه، فرمان‌های آزمایش و عیب‌یابی را توضیح می‌دهد.
