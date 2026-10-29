# Tando dual-eye Wokwi simulator

This simulator builds the same modular firmware as the board, from
`firmware/tando_final_demo_15min/`. The current development candidate is
`v0.10.0-rc.17`; read the repository `VERSION` and the installation report for the
exact source being tested. Product behavior is documented in the [root README](../../README.md).

The Wokwi build selects simulation-only input adapters, a 60 ms frame target,
40 MHz display SPI and the retained ROI renderer. A normal board build keeps its
own defaults. The simulator has no MPR121 or RC522 model: Serial commands call the
shared event handlers and do not test physical sensor detection or RFID latching.

## Windows one-command setup

Use x64 Windows 10/11 with PowerShell 5.1 or newer, and install VS Code first.
The script prepares Git, Python, PlatformIO, Wokwi CLI and the official extension
without administrator access. Run this from any directory:

```powershell
& { $ErrorActionPreference='Stop'; [Net.ServicePointManager]::SecurityProtocol=[Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12; $tandoInstaller=Join-Path ([IO.Path]::GetTempPath()) ('tando-install-'+[guid]::NewGuid().ToString('N')+'.ps1'); Invoke-WebRequest -UseBasicParsing -Uri 'https://raw.githubusercontent.com/vahid162/tendo/5a17c05fe04f280cb7148e0566d567c1ccd86132/simulator/wokwi/install.ps1' -OutFile $tandoInstaller; if ((Get-FileHash -LiteralPath $tandoInstaller -Algorithm SHA256).Hash -ne 'c39152e3615fd638623bb2abefed7a8892c348abe77ce3ce4fdfe0c8795bba88') { throw 'Tando installer SHA-256 mismatch' }; & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $tandoInstaller -Branch 'feature/wokwi-shared-firmware' }
```

The URL selects an immutable installer commit; its SHA-256 is checked before
execution. `Bypass` affects only the child process, not the machine/user execution
policy. Organizational policies may still block execution.

The installer defaults to `develop`. The review command above explicitly selects
`feature/wokwi-shared-firmware` before merge. Once the change is on `develop`, omit
that review override or use `-Branch develop`. Other branches can be selected explicitly. A path on another branch is rejected with instructions to
save the work and switch manually or choose a fresh path. The installer never
switches branches, resets files or stashes your work.

Default checkout: `$env:USERPROFILE\Documents\Tando-Wokwi\tendo`.
Default managed tools: `$env:LOCALAPPDATA\Tando-Wokwi`.
A dirty checkout on the selected branch is built without a Git update. A clean
checkout can fast-forward to the exact fetched commit. Local commits and
divergent history are preserved. The report records branch, commit and dirty state.

The older `feature/wokwi-dual-gc9a01` checkout may contain uncommitted ROI work.
Keep that checkout and use a separate installation path for this candidate.
Do not use a fresh clone as a substitute for saving those local changes.

Enter a personal token from the [Wokwi CI dashboard](https://wokwi.com/dashboard/ci)
when the hidden prompt appears. The token is temporary, redacted from diagnostics
and never saved by the installer. A preconfigured `WOKWI_CLI_TOKEN` is used through
the normal Wokwi CLI route. Leaving it blank, or passing `-SkipSimulation`, prepares
the project with runtime/image results **unverified** and returns exit code `2`.
Exit `1` means failure; `0` means automated runtime/image smoke checks passed.
Human visual review is still required after exit `0`.

## Start in VS Code and send commands

After the folder opens, use `F1` → `Wokwi: Request a new License` if needed,
complete the official browser activation, then `F1` → `Wokwi: Start Simulator`.
The VS Code license is separate from the CLI token; see the account's current
[plan/trial requirements](https://wokwi.com/pricing).

Open `Ctrl+J` → **TERMINAL** and choose **Wokwi Terminal**. Keep the simulator
visible, click that terminal, use the English keyboard, type a command and press
Enter. Input may not echo. A `PS C:\...>` prompt is PowerShell, not ESP32 Serial.

| Command | Action |
|---|---|
| `p`, `1`, `2` | PET, FOOD1, FOOD2 |
| `s`, `u` | Toggle SLEEP/WAKE, unknown RFID event |
| `l`, `w`, `e`, `g` | LOOK, WINK, EYE SMILE, PLAY INVITE |
| `h`, `r`, `b` | Hunger preview, PET Request preview, Blink |
| `i`, `?` | Status, help |
| `v` | Simulator timing/cache diagnostic |
| `f` | Toggle simulator frame target: 60 / 120 ms |
| `o` | Toggle simulator transfer: BANDS / single BBOX |
| `D` | Reset this simulation's demo progress and timer |

The demo keeps three ten-minute stages of Active Demo Time; inactivity and Sleep
retain the shared firmware rules. A short smoke run does not validate the whole demo.

## Performance and validation limits

The retained GC9A01 model batches pixel writes. Its compiled-model regression
covers normal color polarity, split SPI transactions, independent displays and the
round mask. Model callback counts are not measured VS Code FPS.

The simulator renderer keeps two independent previous frames (160,000 heap bytes)
and a split background cache (80,000 more heap bytes), in addition to the existing
80,000-byte canvas. The fixed ring geometry uses another 1,920 bytes, with an
800-byte conservative row-span mask to avoid scanning the empty ring interior.
Narrow transfer rows share one 1,600-byte aligned staging buffer. Allocation
failures use the full rendering/transfer fallback; PSRAM is not required. The first
frame is complete, identical frames send no image data, and changed windows include
erased pixels and preserve aligned RGB565 rows. Up to four horizontal change bands
avoid sending large gaps between graphics; a fifth band falls back to one bbox
before any window is sent.

The fixed renderer corpus measures the following work at its original 120 ms
sample times. It compares identical inputs and images; these are transfer counts,
not a live frame-rate benchmark. Staging uses extra CPU copying to reduce SPI work.

| Work | RC12 | RC13 |
|---|---:|---:|
| Idle ring-background pixel checks per eye pair | 13,568 | 0 |
| PET Request payload, 160 warm eye images | 1,907,992 bytes | 1,257,280 bytes |
| PET Request pixel-write calls, same images | 9,587 | 1,042 |
| PET Request address windows, same images | 160 | 251 |

After rebuilding firmware or the custom chip, stop and restart the simulator.
RC15 prints `SIM CONFIG` once with CPU/APB clocks, SPI divider, PSRAM size and buffer
addresses, then `SIMPERF` automatically every five simulated seconds. Keep the
simulator visible and inspect both eyes; normal use does not require `f`, `o` or
timed `v` commands. Keep the startup/configuration and automatic lines with the
exact source/build identity if reporting a problem. `millis/micros` inside Wokwi
are simulated time; these automatic intervals do not establish real FPS. Live
speed and command latency still need confirmation.

Calculate real frame rate as the difference in `frames` divided by elapsed real
seconds. `pair_us`, `draw_us`, `spi_us` and `bytes` describe the latest frame pair;
`max_loop_ms` is a maximum since boot and can include the initial full-frame transfer.
The 60 ms target permits up to about 16.7 frame pairs per simulated second, but
rendering, SPI and the host simulator can lower the actual rate. Reaction and
demo durations are unchanged.

The owner's RC13 live sample reported 35 eye pairs over ten real seconds while virtual time
advanced only 3.324 seconds: 3.5 real pairs/s and a 0.3324 clock ratio. That run did
not pass smoothness acceptance. RC14 fills rectangle centers by rows to reduce
CPU work. The owner still reports lag on RC14, but did not follow its manual
comparison steps; those line intervals are not a controlled real-time benchmark.
RC15 removes the 12 MHz SPI cap and calculates visual targets only when a frame
is admitted. It keeps the same pixels, 60 ms target and default band transfers.
The driver remains synchronous; a faster bus removes wire waiting, not all render
or host costs. The owner reports a small improvement on RC15, with unacceptable lag remaining.

RC16 added contiguous rounded-eye spans and an aligned-pair delta scan. The owner
reported no visible improvement. Live zero-transfer samples raised scan time from
about 7.7 to 9.8 ms; 1,944 controlled inputs confirmed identical scan regions and
payloads. RC17 restores the previous delta scan and retains the rounded-eye rows.
The earlier WASM improvement did not predict actual Xtensa/ROM comparison cost.

The `cpu_mhz` boot field reports the firmware clock configuration. Wokwi separately
[limits CPU instruction execution](https://docs.wokwi.com/guides/esp32#cpu-frequency-limit):
`cpuFrequency=auto` (the diagram default) caps it at about 8 MHz without changing
peripheral timing. Thus a 240 MHz boot report does not establish 240 MHz of simulated
instruction throughput. The diagram keeps its default; increasing the cap can add
host load and is not a substitute for measuring actual execution speed.

### Automatic host timing

After updating and rebuilding, start Wokwi and keep its simulator tab visible.
In PowerShell in this folder, run:

```powershell
& .\.venv\Scripts\python.exe .\measure_performance.py --seconds 45
```

The installed PlatformIO environment already includes PySerial. The collector
connects to `127.0.0.1:4000` through Wokwi's documented RFC2217 interface and sends
only `v` status requests. The project's `rfc2217ServerPort` enables that serial
server while Wokwi runs; its server binding is controlled by Wokwi. No CLI token
or firmware interaction sequence is required.

Reports go to `.tando-performance/` (ignored by git). They include receipt-time
render-pair throughput and simulated-seconds/real-second ratios. These are firmware
completion counts, not GUI presentation FPS; buffering, pauses and resets must be
considered when interpreting them. If connection fails, check that Wokwi was
restarted with the updated config and port 4000 is available.

The first complete RC17 host capture recorded 153 pairs in 41.45 real seconds,
while the simulated clock advanced 10.171 seconds (3.69 pairs/real second,
clock ratio 0.245). The 60 ms target, BANDS mode and all caches remained enabled.
Before PET, a complete interval recorded 5.04 pairs/real second; the definite PET
portion averaged 3.55. This confirms continued slowdown, but does not assign its
cost to the CPU emulator, display model or GUI. `draw_us`, `spi_us` and `bytes`
describe the latest frame, not all work since the preceding report.

### Automatic transfer comparison

To investigate the tradeoff between fewer transferred pixels and more display
commands, run this in **PowerShell**, where the prompt begins with `PS C:\...>`:

```powershell
& .\.venv\Scripts\python.exe .\measure_performance.py --compare-transfer
```

This uses the same RC17 firmware. Keep Wokwi visible and leave its Serial input
alone during the capture. The tool alternates BANDS and BBOX in a balanced,
repeated order, with eight 12-second measurement windows plus setup and settling.
It sends only `v` and `i` status queries and the existing `o` transfer toggle;
it does not change frame targets, trigger care events or reset demo progress.
It checks mode changes, discards settling frames and attempts to restore and
confirm the original transfer mode on completion, error or Ctrl+C. If restoration
cannot be confirmed, restart Wokwi before continuing ordinary use.

The separate JSON/JSONL comparison reports retain each window's workload
observations. Natural animation and care transitions can affect results; the
tool reports measurements without automatically choosing a permanent mode.
This is a diagnostic experiment, not a verified lag fix or GUI FPS benchmark.

The optional developer controls remain: `f` toggles the 60/120 ms target, and `o`
toggles BANDS/BBOX transfer. Restart restores 60 ms and BANDS. Ordinary diagnosis
uses the automatic collector with the defaults.

The default board renderer, NVS format, pins, tag UIDs, Stage/Care timing and the
four known firmware bugs are not changed by selecting the simulator build.

## Evidence and further development

Read `.tando-install.json`, `sim-smoke.log` and the actual generated screenshots:
`left-idle.png`, `right-idle.png`, `left-pet.png`, `right-pet.png` under `screenshots/`.
The verifier rejects missing/blank/unchanged images and missing boot/event messages,
but synthetic verifier tests and build success do not prove correct live animation.
Inspect both eyes and Blink/PET/FOOD/SLEEP before recording visual acceptance.

The installer builds local `firmware.bin`, `firmware.elf` and `gc9a01.chip.wasm`;
it does not silently substitute another commit's CI artifacts. Missing cloud access
leaves runtime/image evidence unverified. GitHub shares source, not a hosted Wokwi
editor URL.

See [BUILD.md](BUILD.md) for exact CLI commands, [COLLABORATION.md](COLLABORATION.md)
for sharing evidence, and the Persian guide below for the first Windows run.

---

<a id="راهنمای-کامل-فارسی"></a>

## راهنمای فارسی

این شبیه‌ساز از همان فایل‌های ماژولار فریم‌ور در
`firmware/tando_final_demo_15min/` ساخته می‌شود. نسخهٔ کد شاخه و نسخهٔ نصب‌شده را
در `VERSION` و گزارش `.tando-install.json` ببین؛ نسخهٔ RC به معنی Stable نیست.

### نصب و شروع

- VS Code را از قبل نصب کن؛ ویندوز باید ۱۰ یا ۱۱ و x64 باشد.
- دستور واحد بخش [نصب ویندوز](#windows-one-command-setup) را در PowerShell اجرا کن.
- برای این کاندید، مسیر تازه‌ای انتخاب کن تا کارهای ذخیره‌نشدهٔ کلون قدیمی حفظ شوند.
  پیش‌فرض نصب‌کننده `develop` است؛ دستور بررسی بالا صریحاً شاخهٔ
  `feature/wokwi-shared-firmware` را انتخاب می‌کند. پس از ادغام، این گزینهٔ بررسی
  را حذف کن یا `-Branch develop` بده.
- توکن شخصی Wokwi را فقط در ورودی مخفی بنویس. گزینهٔ `-SkipSimulation` یا ورودی
  خالی، نصب آماده با تست اجرای زندهٔ تأییدنشده و کد خروجی ۲ می‌دهد.
- مجوز افزونهٔ VS Code از توکن CLI جداست. در VS Code از `F1` گزینهٔ
  `Wokwi: Request a new License` را بزن و مسیر رسمی مرورگر را کامل کن؛ سپس
  `Wokwi: Start Simulator` را اجرا کن.

### محل فرمان‌ها و اندازه‌گیری سرعت

برای فرمان `p` یا `v`، با `Ctrl+J` پنل ترمینال را باز و از فهرست آن
**Wokwi Terminal** را انتخاب کن. صفحهٔ شبیه‌ساز باز و قابل مشاهده بماند؛ داخل
ترمینال کلیک کن، کیبورد را انگلیسی کن، حرف و Enter را بزن. ممکن است حرف تایپ‌شده
دیده نشود. PowerShell فرمان را به ESP32 نمی‌فرستد.

`p` نوازش، `1/2` غذا، `s` خواب/بیداری، `b` پلک و `i` وضعیت است.
برای اندازه‌گیری، در **PowerShell** همین پوشه دستور بخش
[اندازه‌گیری خودکار](#automatic-host-timing) را اجرا کن. ابتدای خط باید
`PS C:\...>` باشد؛ واردکردن دستور Python در Wokwi Terminal باعث می‌شود حروف
آن به فرمان‌های چشم‌ها تبدیل شوند. زمان‌گیری دستی لازم نیست.

گزینهٔ `--compare-transfer` دو روش ارسال تصویر را خودکار مقایسه می‌کند و در
پایان بازگشت به حالت اولیه را بررسی می‌کند. حدود دو دقیقه برای اجرا در نظر
بگیر و پنجرهٔ شبیه‌ساز را قابل‌مشاهده نگه دار. خروجی PowerShell و فایل‌های
گزارش `.tando-performance/` را برای بررسی نگه دار. این آزمایش با همان RC17
انجام می‌شود و رفع کندی هنوز تأیید نشده است.

### حفظ کار و معنی نتیجه

نصب‌کننده شاخه را خودکار عوض نمی‌کند و فایل‌های محلی را پاک یا Reset نمی‌کند.
اگر مسیر روی شاخهٔ دیگری باشد، متوقف می‌شود. اگر همان شاخه تغییر محلی داشته
باشد، بدون به‌روزرسانی Git همان کد را می‌سازد. `-NoUpdate` نیز صریحاً به‌روزرسانی
را غیرفعال می‌کند. شاخه، Commit و وضعیت تغییرات محلی در گزارش نصب ثبت می‌شوند.

کد خروجی صفر یعنی بررسی خودکار اجرای کوتاه و تصویرها پاس شده است؛ باید تصویر
واقعی هر دو چشم و واکنش‌ها را هم ببینی. کد ۲ یعنی محیط آماده است ولی اجرای زنده
یا تصویر تأیید نشده؛ کد ۱ یعنی مرحله‌ای شکست خورده است. گزارش و پیام بازیابی را
بخوان و خطا را همراه Commit دقیق و شواهدِ بدون اطلاعات محرمانه ثبت کن.

این شبیه‌ساز حسگر واقعی Touch و کارت‌خوان RFID ندارد؛ فرمان‌های Serial مستقیماً
رویداد را فراخوانی می‌کنند. بنابراین تست حساسیت لمس، تعویض واقعی کارت و رفتار
قطع برق هنوز به مسیر تست مناسب و برد نیاز دارند. چهار باگ قبلی در این انتقال
اصلاح نمی‌شوند.
