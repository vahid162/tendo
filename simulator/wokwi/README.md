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

## Windows one-command setup

Use x64 Windows 10/11 and PowerShell 5.1 or newer. Install Visual Studio Code first; Git, Python, PlatformIO, Wokwi CLI, and the Wokwi extension are prepared by [install.ps1](install.ps1). Run this command from any directory:

```powershell
& { $ErrorActionPreference='Stop'; [Net.ServicePointManager]::SecurityProtocol=[Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12; $tandoInstaller=Join-Path ([IO.Path]::GetTempPath()) ('tando-install-'+[guid]::NewGuid().ToString('N')+'.ps1'); Invoke-WebRequest -UseBasicParsing -Uri 'https://raw.githubusercontent.com/vahid162/tendo/d06745afd55140e86b5d375c096336a8115b382d/simulator/wokwi/install.ps1' -OutFile $tandoInstaller; if ((Get-FileHash -LiteralPath $tandoInstaller -Algorithm SHA256).Hash -ne '112a8b8c658fcbb14c9ad38a9ba75165574d0dc1a4efbf95730793306833a3dc') { throw 'Tando installer SHA-256 mismatch' }; & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $tandoInstaller }
```

The download selects an immutable installer commit and checks its SHA-256 before execution. Review the linked script first if required by your organization. `Bypass` applies only to the child PowerShell process; no machine/user execution policy is changed. An enforced organizational policy may still prevent execution.

Accept the default repository path, `$env:USERPROFILE\Documents\Tando-Wokwi\tendo`, or enter another path. A new checkout uses `feature/wokwi-dual-gc9a01`. A clean existing checkout is updated by fast-forward; a dirty checkout keeps its source and is built without an update. The final report records the source commit and dirty state. The installer reuses compatible tools and verifies pinned downloads before installing missing tools in user scope. Its default tool directory is `$env:LOCALAPPDATA\Tando-Wokwi`; PlatformIO and Pillow use the simulator's `.venv`.

When prompted, enter your personal token from the [Wokwi CI dashboard](https://wokwi.com/dashboard/ci). Input is hidden and is not saved. An existing `WOKWI_CLI_TOKEN` is used for the actual service check; merely finding the variable does not prove access. A blank answer prepares the workspace with cloud and image checks marked unverified. The installer returns `2` for this partial result, `1` for a failed stage, or `0` after its runtime and image smoke checks pass. A zero exit code still requires visual review of the eyes.

The CLI token and VS Code license are separate. After the installer opens this folder, use `F1` → `Wokwi: Request a new License` if needed. Complete the official browser sign-in/confirmation flow and wait for the activation message in VS Code. Then use `F1` → `Wokwi: Start Simulator`. The installer installs/detects the extension but cannot certify account activation. The [pricing page](https://wokwi.com/pricing) currently lists VS Code in Hobby+ and offline use in Pro; check your account's license or trial. [CI quotas](https://docs.wokwi.com/wokwi-ci/getting-started) are separate and include a limited free allocation.

## Evidence and subsequent development

The installer builds `firmware.bin` and `firmware.elf` in `.pio/build/tando-s3-sim/` and `gc9a01.chip.wasm` beside `wokwi.toml`. It runs structural tests, diagram lint, local builds, and the [smoke scenario](tests/smoke.yaml). With working cloud access and screenshot support, the scenario captures `screenshots/left-idle.png`, `right-idle.png`, `left-pet.png`, and `right-pet.png`, then exercises FOOD and SLEEP/WAKE through Serial. The verifier rejects missing/invalid/blank images, missing boot/event messages, crashes, and a PET image that does not change. Passing image smoke checks does not prove correct animation or the full 30-minute demo.

Read `sim-smoke.log` and `.tando-install.json` in this folder for the actual result. Open all four screenshots and inspect both live eyes, Blink, PET, FOOD, and SLEEP before recording graphical acceptance. Simultaneous two-eye rendering remains unverified until that real test occurs. If cloud access is unavailable, the prepared VS Code workspace is a practical route for a licensed manual test; it does not replace the missing automated evidence.

After editing `sketch.ino`, rerun `install.ps1` to rebuild and retry. Use `-NoUpdate` to keep the current source, `-InstallPath` for a different checkout, `-SkipSimulation` for an explicitly partial GUI preparation, or `-NoOpen` to suppress the final VS Code open request. See [BUILD.md](BUILD.md) for CLI commands and [COLLABORATION.md](COLLABORATION.md) for sharing changes and evidence.

---

<a id="راهنمای-کامل-فارسی"></a>

# راهنمای کامل فارسی

## ۱. این شبیه‌ساز چه کاری انجام می‌دهد؟

این پوشه، Firmware تندو را روی ESP32-S3-N8R2 و دو نمایشگر گرد GC9A01 با تصویر ۲۴۰×۲۴۰ اجرا می‌کند. نسخهٔ مبنا `v0.10.0-rc.8` است: دموی ۳۰ دقیقه‌ای با سه Stage ده‌دقیقه‌ای. در بررسی مبنا، `develop` روی Commit برابر `f181cc2285b06f70d2cce2113d30d698a2c98ac8` بود و فایل Firmware اصلی با آن اختلاف نداشت. نسخه و Commit نصب خودت در گزارش پایانی ثبت می‌شوند.

در مدار مجازی، حسگر MPR121 و کارت‌خوان RC522 حضور ندارند. فرمان‌های Serial همان تابع‌های رویداد Firmware را فعال می‌کنند. این روش برای توسعهٔ رفتار چشم‌ها مناسب است؛ حساسیت لمس، RFID و سخت‌افزار واقعی باید روی خود دستگاه آزمایش شوند.

## ۲. پیش از شروع

این موارد لازم‌اند:

- Windows 10 یا Windows 11 با معماری x64؛ نسخهٔ ARM64 و ویندوز ۳۲ بیتی در این نصب‌کننده تأیید نشده‌اند.
- Windows PowerShell 5.1 یا جدیدتر و اتصال اینترنت.
- **Visual Studio Code نصب‌شده**؛ نصب خود VS Code وظیفهٔ این اسکریپت نیست.
- حساب Wokwi برای توکن CLI و مجوز مناسب افزونه، در صورت استفاده از آن مسیر.

Git، Python، PlatformIO و Wokwi CLI لازم نیست از قبل نصب باشند. نصب‌کننده ابزار سازگار موجود را شناسایی می‌کند و ابزار کمبود را در حساب کاربر آماده می‌کند. نسخه‌های دانلودی فعلی شامل MinGit `2.56.0.3`، Python `3.13.17` و Wokwi CLI `0.29.1` هستند؛ محیط Python پروژه از PlatformIO `6.2.0` و Pillow `11.3.0` استفاده می‌کند. دانلودها پیش از اجرا با SHA-256 بررسی می‌شوند؛ فایل نصب Python باید امضای معتبر Python Software Foundation نیز داشته باشد.

## ۳. فقط یک دستور اجرا کن

Windows PowerShell را باز کن؛ لازم نیست داخل پوشهٔ خاصی باشی. **دستور بخش [Windows one-command setup](#windows-one-command-setup) در بالای همین صفحه را یک‌جا کپی و اجرا کن.** پیش از اجرا می‌توانی [متن نصب‌کننده](install.ps1) را بخوانی. آدرس دانلود به یک Commit ثابت اشاره می‌کند و اگر هش فایل تغییر کرده باشد اجرا متوقف می‌شود.

گزینهٔ `-ExecutionPolicy Bypass` فقط برای فرایند تازهٔ PowerShell است و تنظیم امنیتی سراسری ویندوز را عوض نمی‌کند. اگر سازمانت اجرای اسکریپت را با سیاست اجباری منع کرده باشد، همان سیاست برقرار می‌ماند.

نصب‌کننده ابتدا VS Code را پیدا می‌کند و سپس مسیر مخزن را می‌پرسد. برای مسیر پیش‌فرض فقط Enter بزن:

```text
%USERPROFILE%\Documents\Tando-Wokwi\tendo
```

برای مسیر دیگر، مسیر کامل دلخواهت را در همان ترمینال وارد کن. ابزارهای مدیریت‌شده در `%LOCALAPPDATA%\Tando-Wokwi` و محیط Python پروژه در `simulator\wokwi\.venv` قرار می‌گیرند. دانلود اولیهٔ زنجیرهٔ ساخت ESP32 ممکن است زمان ببرد؛ پس از پایان هر مرحله پیام وضعیت آن را می‌بینی.

نمونهٔ پیام‌های موفق، بسته به مراحل واقعاً اجراشده:

```text
[OK] Visual Studio Code detected
[OK] Git ready
[OK] Python ready
[OK] PlatformIO ready
[OK] Wokwi CLI ready
[OK] Wokwi extension installed/detected (license status unverified)
[OK] Firmware compiled and binary signatures verified
[OK] GC9A01 custom chip compiled and WASM signature verified
```

پیام `UNVERIFIED` یعنی آن آزمون تأیید نشده است. پیام `prepared` فقط آماده‌شدن محیط را نشان می‌دهد. پیام موفق ساخت یا `EYES READY` به‌تنهایی به معنی درست‌بودن تصویر دو چشم نیست.

## ۴. توکن Wokwi را امن وارد کن

از [داشبورد CI حساب خودت](https://wokwi.com/dashboard/ci) یک توکن شخصی بگیر. هنگام نمایش `Wokwi API Token`، مقدار آن را در همان PowerShell وارد کن. ورودی مخفی است؛ توکن را در فایل پروژه، پیام Commit، Issue، PR یا تصویر ترمینال قرار نده.

نصب‌کننده توکن را برای فرایند تست استفاده می‌کند و آن را به‌صورت دائمی ذخیره نمی‌کند. اگر `WOKWI_CLI_TOKEN` از قبل در محیط باشد، مقدار همان متغیر استفاده می‌شود؛ اعتبار آن فقط با پاسخ واقعی سرویس مشخص می‌شود. خطای دسترسی باید برطرف و آزمون تکرار شود.

اگر توکن نداری، ورودی را خالی بگذار. محیط برای آزمایش دستی در VS Code آماده می‌شود، اما اجرای ابری و تصاویر تأییدنشده می‌مانند. کد خروجی `2` همین نتیجهٔ ناقص را نشان می‌دهد؛ کد `1` نشانهٔ شکست یک مرحله است. کد `0` فقط موفقیت بررسی‌های خودکار را نشان می‌دهد و مشاهدهٔ انسانی همچنان لازم است.

طبق [مستندات فعلی CI](https://docs.wokwi.com/wokwi-ci/getting-started)، حساب رایگان سهمیهٔ محدود ۵۰ دقیقه در پنجرهٔ شناور ۳۰روزه دارد؛ طرح‌های پولی سهمیهٔ بیشتری دارند. اجرای CLI، Firmware را برای شبیه‌سازی به سرویس ابری Wokwi می‌فرستد. دسترسی اینترنت و سهمیهٔ حساب برای این مرحله لازم‌اند.

## ۵. افزونهٔ VS Code را فعال کن

نصب‌کننده افزونهٔ رسمی `Wokwi.wokwi-vscode` را نصب یا شناسایی می‌کند. **توکن CLI و مجوز افزونه دو مورد جدا هستند.** نصب افزونه به معنی فعال‌شدن حساب نیست.

اگر مجوز افزونه فعال نیست، [مراحل رسمی فعال‌سازی](https://docs.wokwi.com/vscode/getting-started) را انجام بده:

1. در VS Code کلید `F1` را بزن و `Wokwi: Request a new License` را انتخاب کن.
2. بازشدن سایت Wokwi در مرورگر را تأیید کن؛ در سایت وارد حساب خودت شو و دریافت مجوز را ادامه بده.
3. اجازهٔ بازگشت مجوز از مرورگر به VS Code را تأیید کن.
4. فقط پس از دیدن پیام `License activated`، فعال‌سازی را موفق در نظر بگیر.

[صفحهٔ طرح‌های Wokwi](https://wokwi.com/pricing) در زمان نگارش، VS Code را در طرح Hobby+ و افزونهٔ آفلاین را در Pro فهرست می‌کند. شرایط مجوز یا دورهٔ آزمایشی حساب خودت را بررسی کن؛ ایجاد حساب رایگان به‌تنهایی تضمین دسترسی افزونه نیست. نصب‌کننده خرید، ورود مرورگری یا تأیید مجوز را به‌جای تو انجام نمی‌دهد.

## ۶. دو چشم را زنده ببین

در پایان نصب، پوشهٔ `simulator\wokwi` با CLI در VS Code باز می‌شود. اگر Workspace Trust نمایش داده شد، فقط پس از بررسی منبع مخزن دربارهٔ اعتماد تصمیم بگیر. سپس `F1` را بزن و `Wokwi: Start Simulator` را اجرا کن.

باید برد ESP32-S3 و هر دو نمایشگر را در مدار ببینی. در Serial Monitor با سرعت `115200`، پیام‌های Firmware و راهنما را بررسی کن. روی ورودی Serial تمرکز کن و هر فرمان زیر را جداگانه بفرست:

| فرمان | آزمایش |
|---|---|
| `p` | نوازش یا PET؛ هر دو چشم باید واکنش مهربانانه نشان دهند |
| `1` و `2` | FOOD با دو تگ غذا |
| `s` | بار اول خواب پایدار؛ بار دوم برداشتن تگ و بیدارشدن |
| `b` | پلک‌زدن؛ هنگام واکنش واقعی یا Sleep Request ممکن است مسدود شود |
| `w` | چشمک یک‌طرفه؛ با پلک‌زدن معمولی فرق دارد |
| `u` | RFID ناشناس |
| `l` | نگاه به اطراف |
| `e` | لبخند چشم |
| `g` | دعوت به بازی |
| `h` | پیش‌نمایش درخواست غذا |
| `r` | پیش‌نمایش درخواست نوازش |
| `i` | وضعیت Demo و Stage |
| `D` | صفرکردن پیشرفت و زمان ذخیره‌شدهٔ همین شبیه‌ساز |
| `?` | راهنمای Serial |

برای آزمایش پایه، `p` را بفرست، پایان واکنش را صبر کن، سپس `1` و بعد `s` را امتحان کن. چند لحظه بعد دوباره `s` بفرست تا بیدارشدن دیده شود. PET و FOOD در خواب پایدار پذیرفته نمی‌شوند؛ این رفتار مطابق Firmware محصول است. حرکت Idle، Blink و واکنش هر دو نمایشگر را با چشم بررسی کن. زمان Demo بعد از تعامل شروع می‌شود و پس از ۶۰ ثانیه بی‌تعاملی Pause می‌شود؛ این تست کوتاه، کل دموی ۳۰ دقیقه‌ای را پوشش نمی‌دهد.

## ۷. نتیجه و Screenshotها را بررسی کن

پس از ساخت، این فایل‌ها باید وجود داشته باشند:

```text
simulator/wokwi/.pio/build/tando-s3-sim/firmware.bin
simulator/wokwi/.pio/build/tando-s3-sim/firmware.elf
simulator/wokwi/gc9a01.chip.wasm
```

با توکن معتبر و پشتیبانی موتور از Screenshot، سناریوی خودکار این چهار تصویر را می‌سازد:

```text
simulator/wokwi/screenshots/left-idle.png
simulator/wokwi/screenshots/right-idle.png
simulator/wokwi/screenshots/left-pet.png
simulator/wokwi/screenshots/right-pet.png
```

فایل‌های PNG را در Explorer یا VS Code باز کن. چشم چپ و راست باید هر دو تصویر قابل‌مشاهده داشته باشند و PET با Idle فرق داشته باشد. بررسی خودکار، تصویر سیاه/خالی/شفاف، ابعاد نامعتبر و تغییرنکردن PET را رد می‌کند؛ تشخیص درست‌بودن شکل، جهت، هماهنگی و انیمیشن به مشاهدهٔ واقعی نیاز دارد. اگر تصویر تولید نشد یا موتور گرفتن تصویر این قطعهٔ سفارشی را پشتیبانی نکرد، نتیجهٔ تصویری تأیید نشده است.

`sim-smoke.log` خروجی Serial و `.tando-install.json` خلاصهٔ نصب را در همین پوشه نگه می‌دارند. گزارش شامل نسخه، Commit، وضعیت تغییرات محلی، ابزارها، نتیجهٔ آزمون‌ها، مرحلهٔ شکست و راه بازیابی است. فایل‌های تولیدی و توکن را وارد Git نکن. پیش از ارسال لاگ برای همکاری، محتوای آن را بازبینی کن.

**وضعیت پذیرش گرافیکی این پروژه هنوز نیازمند ثبت مشاهدهٔ واقعی هم‌زمان دو چشم است.** آزمون‌های ساختاری، کامپایل و داده‌های ساختگیِ تست اعتبارسنج تصاویر، مدرک مشاهدهٔ واقعی چشم‌ها محسوب نمی‌شوند.

## ۸. کد را تغییر بده و دوباره اجرا کن

برای تغییر شبیه‌ساز، `sketch.ino` را ویرایش کن. مدل نمایشگر در `gc9a01.chip.c` و سیم‌کشی مجازی در `diagram.json` است. رفتار اصلی محصول در فایل ریشهٔ مخزن قرار دارد و نباید برای عبور از تست شبیه‌ساز تغییر داده شود.

در ترمینال VS Code که مسیرش همین پوشه است، نصب‌کننده را دوباره اجرا کن:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\install.ps1 -InstallPath (Resolve-Path ..\..).Path -NoUpdate
```

این اجرا ابزار موجود را دوباره نصب نمی‌کند، خروجی‌های لازم را از کد فعلی می‌سازد و تست را تکرار می‌کند. برای آماده‌سازی GUI بدون تست ابری، `-SkipSimulation` اضافه کن؛ نتیجه همچنان ناقص و کد خروجی `2` خواهد بود. برای بازنشدن پنجرهٔ VS Code، `-NoOpen` اضافه کن. دستورهای ساخت دستی در [BUILD.md](BUILD.md) آمده‌اند. پس از ساخت، شبیه‌ساز را دوباره شروع کن تا Firmware جدید بارگذاری شود.

## ۹. به‌روزرسانی و همکاری تیمی

اجرای دوبارهٔ دستور نصب، مخزن تمیز را فقط با fast-forward به‌روز می‌کند. اگر تغییر محلی داشته باشی، نصب‌کننده آن را نگه می‌دارد و از همان کد موجود می‌سازد. برای جلوگیری صریح از به‌روزرسانی، `-NoUpdate` را به اجرای محلی اضافه کن. اگر تاریخچهٔ محلی و GitHub از هم جدا شده باشند، نصب‌کننده آن‌ها را بازنویسی نمی‌کند؛ ابتدا تغییرها را بررسی و Commit کن و تعارض را آگاهانه حل کن.

برنامه‌نویس دوم همین دستور نصب را با حساب خودش اجرا می‌کند. برای مقایسهٔ خطاها، Commit، تغییرات محلی، نسخهٔ ابزارها و چهار تصویر هر دو نفر باید مشخص باشند. تغییرها را در شاخهٔ مناسب و PR به اشتراک بگذارید؛ توکن‌ها مشترک نیستند. گردش کار و شواهد لازم در [COLLABORATION.md](COLLABORATION.md) توضیح داده شده است. GitHub محل اشتراک کد است؛ آدرس شاخه، لینک یک پروژهٔ میزبانی‌شده در ویرایشگر آنلاین Wokwi نیست.

## ۱۰. عیب‌یابی

| پیام یا وضعیت | اقدام بعدی |
|---|---|
| VS Code پیدا نشد | VS Code را نصب کن یا نصب استاندارد آن را بررسی کن؛ سپس دستور را تکرار کن. |
| خطای اینترنت، GitHub یا دانلود | اتصال، Proxy، ساعت ویندوز و دسترسی به دامنهٔ نشان‌داده‌شده را بررسی کن. مرحلهٔ شکست‌خورده را دوباره اجرا کن. |
| خطای SHA-256 یا امضای Python | فایل تأیید نشده اجرا نمی‌شود. منبع، ارتباط و ساعت سیستم را بررسی کن؛ بررسی هش را حذف نکن. |
| نصب Git/Python/Wokwi ناموفق | فضای دیسک، قرنطینهٔ آنتی‌ویروس و خطای همان مرحله را بررسی کن. اگر نصب صریحاً Restart خواست، ویندوز را Restart و دوباره اجرا کن. |
| PlatformIO Build شکست خورد | نخستین خطای کامپایل یا دریافت وابستگی را بخوان؛ پس از اصلاح، همان دستور را تکرار کن. |
| Firmware یا WASM پیدا نشد | مرحلهٔ ساخت کامل نشده است؛ مسیرهای بخش ۷ و خروجی Build را بررسی کن. |
| Token/access rejected، خطای 401/403 یا سهمیه | توکن حساب خودت، وضعیت دسترسی و سهمیهٔ CI را در Wokwi بررسی و دوباره وارد کن. |
| افزونه نصب است ولی اجرا مجوز می‌خواهد | فعال‌سازی بخش ۵ را کامل کن؛ توکن CLI این مشکل را حل نمی‌کند. |
| Firmware بوت می‌شود ولی یک یا هر دو چشم سیاه‌اند | نتیجهٔ گرافیکی ناموفق است. خروجی Serial، هر دو قطعه و اتصال‌های CS/DC/RST/SCLK/MOSI را بررسی کن؛ Build موفق کافی نیست. |
| Screenshot ساخته نشد یا PET تغییر نکرد | `sim-smoke.log`، پشتیبانی موتور از تصویر قطعه و رویداد PET را بررسی کن؛ در VS Code هر دو چشم را دستی آزمایش کن و نتیجهٔ خودکار را تأییدنشده نگه دار. |
| Panic، Backtrace یا خطای SPI | لاگ را نگه دار و علت واقعی را در محدودهٔ شبیه‌ساز اصلاح کن؛ پیام Boot را موفقیت نهایی حساب نکن. |
| تغییرات محلی یا Git divergence | فایل‌هایت حفظ می‌شوند. پس از بررسی و Commit، تعارض را حل کن؛ برای ساخت کد فعلی از `-NoUpdate` استفاده کن. |

در Arduino-ESP32 مبنای فعلی، هشدار `spiAttachMISO(): HSPI Does not have default pins on ESP32S3!` از درخواست MISO با مقدار `-1` برای گذرگاه خروجی نمایشگر می‌آید. نمایشگر از SCLK و MOSI استفاده می‌کند و این پیام به‌تنهایی مدرک خرابی تصویر نیست. خطاهای دیگر SPI یا تصویر خراب را جداگانه بررسی کن؛ وجود همین هشدار، تأیید درست‌بودن دو چشم هم نیست.

## ۱۱. منابع رسمی

- [نصب Wokwi CLI](https://docs.wokwi.com/wokwi-ci/cli-installation)
- [فرمان‌ها و توکن CLI](https://docs.wokwi.com/wokwi-ci/cli-usage)
- [سناریوها و Screenshot](https://docs.wokwi.com/wokwi-ci/automation-scenarios)
- [معماری ابری و سهمیه‌های CI](https://docs.wokwi.com/wokwi-ci/getting-started)
- [فعال‌سازی Wokwi در VS Code](https://docs.wokwi.com/vscode/getting-started)
- [طرح‌ها و مجوزهای Wokwi](https://wokwi.com/pricing)
- [PlatformIO](https://docs.platformio.org/en/latest/core/index.html)
- [Python روی ویندوز](https://docs.python.org/3/using/windows.html)
- [Git for Windows](https://gitforwindows.org/)
