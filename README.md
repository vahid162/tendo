# تندو (Tando)

تندو یک ربات/اسباب‌بازی تعاملی بر پایه ESP32-S3 است که با دو نمایشگر گرد، سنسور لمسی خازنی، RFID و LED به رفتار کاربر واکنش نشان می‌دهد.

این مخزن برای توسعه Firmware نسخه دموی ۳۰ دقیقه‌ای تندو استفاده می‌شود.

## وضعیت فعلی

نسخه Pre-release فعلی Firmware: **v0.10.0-rc.3**

آخرین نسخه Stable: **v0.7.1**

فایل اصلی Firmware:

```text
tando_final_demo_15min.ino
```

از این فایل به‌عنوان مبنای تغییرهای بعدی استفاده می‌کنیم.

ویژگی‌های فعال:

- دو چشم روی دو نمایشگر GC9A01 با رزولوشن 240×240
- حرکت زنده و بازیگوش چشم‌ها در حالت Idle با cadence آرام‌تر در Stage 2 و Stage 3
- پلک‌زدن خودکار با بسته‌شدن غالب از پلک بالا (Top-Lid-Dominant)
- واکنش نوازش با MPR121
- تشخیص PET با حداقل ۲ الکترود از E0 / E6 / E11
- حداقل زمان حضور خازنی برای PET: حدود ۱ ثانیه
- State Manager جدید برای جلوگیری از گم‌شدن واکنش‌ها هنگام اجرای انیمیشن دیگر
- غیرفعال شدن PET در حالت Sleep
- صف مستقل برای Stage 2 / Stage 3 / Completion
- حساسیت فعلی MPR121:
  - Touch Threshold = 6
  - Release Threshold = 3
- دو تگ FOOD
- یک تگ SLEEP
- خواب پایدار وابسته به حضور SLEEP TAG
- حلقه Progress متقارن دور هر دو چشم
- سه Stage ده‌دقیقه‌ای
- مجموع زمان Demo برابر ۳۰ دقیقه Active Time
- لایه شخصیت خودکار عمومی با چهار خانواده رفتار: Look Around / Wink / Eye Smile / Play Invite
- زمان‌بندی نامنظم رویدادهای شخصیت عمومی در سه کلاس 8–20، 20–55 و 55–120 ثانیه
- انتخاب وزن‌دار با Jitter، کاهش احتمال تکرار ۲–۳ رفتار اخیر و افزایش زمینه‌ای احتمال Play Invite در سکوت طولانی‌تر
- Hunger دیگر عضو Pool عمومی Random نیست و یک Scheduler مستقل Stage-aware دارد
- ذخیره Stage، Progress و زمان Demo در NVS
- PWM برای LED واکنش روی GPIO21

## منطق Demo

### Stage 1
از دقیقه 0 تا 10

### Stage 2
از دقیقه 10 تا 20

### Stage 3
از دقیقه 20 تا 30

### Completion
در پایان 30 دقیقه:

- Progress روی 100٪ قرار می‌گیرد.
- Stage روی 3 باقی می‌ماند.
- تعامل‌ها همچنان واکنش دارند.
- Progress دیگر افزایش پیدا نمی‌کند.

در هر Stage فقط یک Credit برای هر کلاس مراقبت ثبت می‌شود:

- PET
- FOOD
- SLEEP

در نتیجه هر Stage حداکثر 3 Credit دارد و کل Demo برابر 9 Credit است.


> نام فایل اصلی Firmware فعلاً برای حفظ سازگاری ابزار Release همان `tando_final_demo_15min.ino` باقی مانده است؛ محتوای فعلی آن Demo سی‌دقیقه‌ای را پیاده‌سازی می‌کند.

## اصلاح سازگاری Arduino IDE در v0.8.0-rc.3

در نسخه `v0.8.0-rc.2` خود Arduino IDE هنگام Preprocess فایل `.ino` برای توابعی که پارامتر یا خروجی `AutonomousState` داشتند Prototype خودکار تولید می‌کرد. این Prototypeها ممکن بود قبل از تعریف Enum قرار بگیرند و خطای زیر ایجاد شود:

```text
'AutonomousState' was not declared in this scope
```

در `v0.8.0-rc.3` نوع داخلی `AutonomousState` حفظ شده، اما Boundary توابع Autonomous از `uint8_t` استفاده می‌کند. در نتیجه Prototypeهای خودکار Arduino دیگر به تعریف زودتر Enum وابسته نیستند. منطق Runtime، Random Scheduler و رفتارهای چشمی نسبت به rc.2 تغییر نکرده‌اند.

## شخصیت خودکار در Idle

در `v0.8.0-rc.1` رفتار Idle به دو لایه تقسیم شده است:

```text
Micro Idle
→ نگاه‌های کوچک، Micro movement و Blink طبیعی

Autonomous Personality
→ Look Around / Wink / Eye Smile / Play Invite

Stage-aware Hunger Request
→ 10-second chicken-drumstick Hunger overlay on both displays, up to 15 completed prompts per Stage until FOOD
```

رویدادهای شخصیت خودکار عمومی هیچ Credit، Progress یا LED interaction pulse ایجاد نمی‌کنند و Active Demo Time را نیز Resume نمی‌کنند. هر تعامل واقعی کاربر یا رویداد System، Visual خودکار جاری را حذف می‌کند؛ رفتار خودکار قدیمی بعد از Reaction در صف پخش نمی‌شود و Scheduler با یک Delay تصادفی جدید شروع می‌شود.

Hunger از این Pool جدا شده و طبق سناریوی مراقبتی Stage اجرا می‌شود.

زمان رویداد عمومی بعدی از سه کلاس نامنظم انتخاب می‌شود:

```text
SHORT  = 8–20 s
MEDIUM = 20–55 s
LONG   = 55–120 s
```

انتخاب رفتار عمومی Sequence ثابت ندارد. وزن پایه با Jitter هر چرخه تغییر می‌کند، رفتارهای ۲–۳ انتخاب اخیر موقتاً وزن کمتری می‌گیرند و احتمال Play Invite با طولانی‌ترشدن سکوت بیشتر می‌شود.

چهار رفتار عمومی Look Around / Wink / Eye Smile / Play Invite در همه Stageها باقی می‌مانند و شدت Smile/Glow/Playfulness در Stage 2 و Stage 3 بیشتر می‌شود. Hunger مستقل از این Pool و تابع Scheduler مراقبتی همان Stage است.


## Hunger Request / درخواست غذا

در نسخه فعلی، Hunger یک Overlay پایین نمایشگر است و جای چشم را نمی‌گیرد:

```text
نمایشگر چپ  → چشم + ران مرغ متحرک
نمایشگر راست → چشم + ران مرغ متحرک
```

در هر Stage حداکثر **10 Hunger Request** کامل وجود دارد و هر Request دقیقاً **10 ثانیه** نمایش داده می‌شود، مگر اینکه یک Reaction واقعی آن را قطع کند. اولین FOOD معتبر همان Stage، Hunger را فوراً خاموش می‌کند، FOOD Progress را طبق قانون Stage ثبت می‌کند و تمام Hunger Requestهای باقی‌مانده همان Stage را لغو می‌کند.

### زمان‌بندی Care Requestها

از `v0.10.0-rc.3`، Hunger و Pet Request دیگر با wall-clock `millis()` زمان‌بندی نمی‌شوند. هر دو فقط با **Active Demo Time همان Stage** جلو می‌روند:

```text
قبل از اولین Interaction      → هیچ Care Request اجرا نمی‌شود
Demo clock RUNNING            → Scheduler جلو می‌رود
Demo clock PAUSED             → Scheduler و Request فعال متوقف/مخفی می‌شوند
Interaction بعدی / Resume     → Scheduler از Active Time ادامه می‌دهد
```

هر Stage ده‌دقیقه‌ای به 10 Slot یک‌دقیقه‌ای تقسیم شده است. در هر Slot یک Need در پنجره Early و دیگری در پنجره Late قرار می‌گیرد و ترتیب آن‌ها بین Slotها عوض می‌شود:

```text
Early window = ثانیه 5 تا 15 همان دقیقه، Random
Late window  = ثانیه 35 تا 45 همان دقیقه، Random
Request      = 10 ثانیه
```

بنابراین Requestها به‌جای اینکه در 3–4 دقیقه اول تمام شوند، روی کل Stage پخش می‌شوند. اگر پنجره‌ای به‌علت Reaction طولانی یا Reboot از دست برود، Firmware آن Slot را Skip می‌کند و Requestهای قدیمی را پشت‌سرهم فشرده نمی‌کند.

اگر یک Request توسط Reaction واقعی قطع شود، Count مصرف نمی‌شود. Retry فقط **بعد از پایان Reaction** و با 5–10 ثانیه Active Time فاصله Schedule می‌شود. بعد از پایان طبیعی هر Care Request نیز حداقل 5 ثانیه Active Time Cooldown برای جلوگیری از چسبیدن دو Request به هم وجود دارد.

## Pet Request / درخواست نوازش

در هر Stage تا قبل از اولین PET معتبر، حداکثر **10 Pet Request** کامل و هر بار **10 ثانیه** وجود دارد.

Visual:

```text
چشم‌ها باقی می‌مانند
→ نگاه نرم کمی بالا/داخل

پایین هر دو نمایشگر
→        ♥
→      ( ♥ )
→     (( ♥ ))
→      ( ♥ )
→        ♥
```

قلب Pulse می‌کند و دو موج پرانتزی دو طرف آن مثل هشدار عاطفی «نازم کن» باز/بسته می‌شوند. اولین PET معتبر، Pet Request را فوراً قطع می‌کند، PET Progress را طبق قانون Stage ثبت می‌کند، تمام Pet Requestهای باقی‌مانده همان Stage را لغو می‌کند و PET Reaction واقعی را اجرا می‌کند.

Hunger و Pet Request هیچ‌وقت هم‌زمان Render نمی‌شوند. اگر Retry یا Delay باعث شود هر دو هم‌زمان Due شوند، یکی Slot را می‌گیرد و دیگری Defer می‌شود.

### اولویت بصری Reaction

Reaction واقعی همیشه بالاتر از Care Request و Autonomous است:

```text
PET / FOOD / SLEEP / UNKNOWN / UNLOCK / COMPLETE
→ فقط همان Reaction روی نمایشگر
→ Hunger مخفی
→ ((Heart)) مخفی
→ Look/Wink/Smile/Play متوقف
→ Progress Ring نیز تا پایان Reaction مخفی
```

Blink باقی‌مانده از قبل نیز هنگام شروع Reaction پاک می‌شود؛ فقط Blink عمدی داخل FOOD Reaction اجازه اجرا دارد.

### Sleep Visual Lock

وقتی `R_SLEEP` فعال است و SLEEP tag هنوز حاضر است:

```text
PET             → نمایش داده نمی‌شود
FOOD            → نمایش داده نمی‌شود
Unknown RFID    → نمایش داده نمی‌شود
Hunger          → نمایش داده نمی‌شود
Pet Request     → نمایش داده نمی‌شود
Autonomous      → نمایش داده نمی‌شود
Stage Unlock    → فقط Pending می‌ماند
Completion      → فقط Pending می‌ماند
```

یعنی Sleep دیگر توسط Stage Unlock یا Completion هم Preempt نمی‌شود. بعد از برداشتن SLEEP tag و تمام شدن Wake، رویدادهای System که Pending مانده‌اند به‌ترتیب سرویس می‌شوند.

Serial:
- `h` = Preview ده‌ثانیه‌ای Hunger، بدون Count/Progress
- `r` = Preview ده‌ثانیه‌ای Pet Request، بدون Count/Progress
- `i` = Status؛ علاوه بر Count، Slot و وضعیت `RUNNING / WAIT_ACTIVE_CLOCK / STOPPED` را نشان می‌دهد.

## رفتار PET / نوازش

از `v0.8.0-rc.4` قانون PET ساده و مستقیم است:

- الکترودهای PET همان E0، E6 و E11 هستند.
- اگر **در وضعیت زنده MPR121 هر دو الکترود یا هر سه الکترود هم‌زمان `YES` باشند، PET معتبر است**.
- فقط یک نمونه 20 ms دیگر برای تأیید پایداری لازم است؛ شرط تجمع حدود 1 ثانیه حذف شده است.
- ترتیب لمس، Session history، `fullRelease` و `startBlockedMask` دیگر در Qualification دخالت ندارند.
- یک الکترود به‌تنهایی هرگز PET ایجاد نمی‌کند.
- بعد از Trigger، همان نگه‌داشتن پیوسته دو/سه الکترود فقط یک Event می‌دهد تا هر 20 ms تکرار نشود.
- برای PET بعدی کافی است تعداد Touchهای زنده برای حداقل 220 ms به کمتر از دو برسد. بنابراین باقی‌ماندن یک Electrode روی `YES` سیستم را قفل نمی‌کند.
- PET در حالت Sleep همچنان غیرفعال است.

نمونه:

```text
E0=YES + E6=YES              -> PET
E0=YES + E11=YES             -> PET
E6=YES + E11=YES             -> PET
E0=YES + E6=YES + E11=YES   -> PET

فقط یک YES                    -> NO PET
```

این منطق عمداً روی **Current Touch Count** کار می‌کند، نه روی یک Session تاریخی.

واکنش چشم در PET:

- نگاه واضح به بالا
- نگاه کمی به داخل
- حالت مهربان‌تر
- Glow نرم
- بازگشت نرم به Idle


## مدیریت واکنش‌ها

در نسخه v7 ورودی‌ها دیگر فقط به دلیل فعال بودن یک انیمیشن کوتاه از دست نمی‌روند.

اولویت کلی:

```text
System: Completion / Stage Unlock
        ↓
Sleep (حالت پایدار تا زمان حذف Tag)
        ↓
Food
        ↓
Pet
        ↓
Unknown RFID
        ↓
Autonomous Personality
        ↓
Micro Idle
```

اگر PET یا FOOD هنگام اجرای یک واکنش کوتاه دیگر برسد، رویداد ثبت می‌شود و Visual آن به‌صورت Pending اجرا می‌شود. تکرارهای هم‌کلاس Coalesce می‌شوند تا صفی از انیمیشن‌های قدیمی ایجاد نشود.

در `v0.7.2-rc.8` بین واکنش‌های کاربر یک handoff کوتاه حدود 240 ms وجود دارد تا Blendهای حالت قبلی، مثل Happy/Surprise/Glow، وارد واکنش بعدی نشوند.

رویدادهای System مانند Stage Unlock و Completion اکنون می‌توانند Visual خواب پایدار را قطع کنند. اگر SLEEP TAG هنوز حاضر باشد، بعد از تمام‌شدن رویداد System حالت Sleep دوباره ادامه پیدا می‌کند.

RFID نیز هنگام اجرای PET/FOOD همچنان Poll می‌شود و دیگر برای چند ثانیه کور نمی‌شود.

## خواب

SLEEP یک State واقعی است:

```text
SLEEP TAG PRESENT -> ASLEEP
SLEEP TAG REMOVED -> WAKE UP
```

تا زمانی که SLEEP TAG جلوی RC522 باشد، چشم‌ها در خواب می‌مانند.

بعد از حذف کامل Tag، چشم‌ها آرام باز می‌شوند. زمان Auto Blink بعد از Wake دوباره زمان‌بندی می‌شود تا بلافاصله بعد از بازشدن چشم، Blink ناخواسته اجرا نشود.

## اصلاحات حرکت چشم در v0.7.2-rc.8

- Blink عادی مانند Sleep به‌صورت Top-Lid-Dominant رسم می‌شود و دیگر Eye Shape از بالا و پایین به مرکز جمع نمی‌شود.
- Unknown RFID به‌جای Shake سریع و مخالف دو چشم، یک نگاه کنجکاوانه و آرام‌تر دارد.
- حرکت ریز Food در فاز خوردن کندتر شده است تا بیشتر شبیه chewing باشد و کمتر شبیه jitter دیده شود.
- فاصله تغییر Targetهای Idle در Stage 2 و Stage 3 افزایش یافته است تا شخصیت چشم بی‌قرار نشود.
- رنگ Progress Ring هنگام `R_UNLOCK2` و `R_UNLOCK3` از خود Reaction گرفته می‌شود تا Unlock قدیمی با رنگ Stage جدید نمایش داده نشود.
- Completion و Stage Unlock در برابر Sleep دارای اولویت واقعی هستند.

## RFID

UIDهای فعلی:

```text
FOOD 1 = 96 2B CD AB
FOOD 2 = F6 33 11 AA
SLEEP  = C6 34 BD AA
```

## Pin Map

### نمایشگرها

```text
TFT SCLK  = GPIO4
TFT MOSI  = GPIO5

LEFT DC   = GPIO6
LEFT CS   = GPIO7
LEFT RST  = GPIO15

RIGHT DC  = GPIO18
RIGHT CS  = GPIO16
RIGHT RST = GPIO17
```

### MPR121

- راه‌اندازی و سنجش MPR121 دقیقاً به مسیر بهترِ rc.3 برگشته است: `Touch=6`، `Release=3` و `Autoconfig=true` مستقیماً در `mpr.begin(...)` اعمال می‌شوند و هیچ تنظیم آزمایشی روی `NHDT/NCLT/FDLT` نوشته نمی‌شود.
- در `v0.7.2-rc.7` سه Zone نوازش برای تست A/B سخت‌افزاری روی کانال‌های دور از هم `E0 / E6 / E11` قرار گرفته‌اند.

```text
SDA = GPIO8
SCL = GPIO9
```

### RC522

```text
SCK  = GPIO10
MISO = GPIO11
MOSI = GPIO12
SS   = GPIO13
RST  = GPIO14
```

### LED

```text
PWM = GPIO21
```

> در سخت‌افزار فعلی از LED کم‌مصرف استفاده می‌شود. اتصال ساده است: `GPIO21 → مقاومت سری مناسب → LED → GND`. مقدار مقاومت باید طوری انتخاب شود که جریان LED و GPIO در محدوده امن باقی بماند.

## کتابخانه‌های مورد نیاز

- Adafruit GFX Library
- Adafruit GC9A01A
- Adafruit MPR121
- MFRC522
- Preferences
- SPI
- Wire

## Serial Monitor

Baud Rate:

```text
115200
```

دستورهای تست فعلی:

```text
p = شبیه‌سازی PET
1 = شبیه‌سازی FOOD 1
2 = شبیه‌سازی FOOD 2
s = شبیه‌سازی حضور/حذف SLEEP TAG
u = RFID ناشناس
b = Blink
i = نمایش وضعیت Demo
t = نمایش Diagnostic خام E0/E6/E11 در MPR121
c = کالیبراسیون دستی MPR121 با همان تنظیمات rc.3 (دست از الکترودها دور باشد)
D = پاک کردن Progress و زمان Demo از NVS
? = Help
```

برای بررسی حساسیت Touch، دستور `t` مقدارهای زیر را نشان می‌دهد:

- baseline
- filtered
- delta
- touched

## Active Demo Time

زمان Demo فقط هنگام روشن بودن دستگاه و فعال بودن Demo Clock محاسبه می‌شود.

اگر برای مدت مشخصی هیچ Interaction وجود نداشته باشد، Demo Clock Pause می‌شود.

خاموش بودن دستگاه جزو 30 دقیقه Demo حساب نمی‌شود.

## حافظه

برای ذخیره وضعیت از Preferences / NVS داخلی ESP32 استفاده می‌شود.

در `v0.8.0-rc.1` نسخه State داخلی NVS از 3 به 4 افزایش یافته است. چون معنای Timing از Demo پانزده‌دقیقه‌ای به Demo سی‌دقیقه‌ای تغییر کرده، State ذخیره‌شده نسخه‌های قدیمی در اولین Boot این نسخه Reset می‌شود تا Stage/Completion قبلی با زمان‌بندی جدید اشتباه تفسیر نشود.

موارد مهم ذخیره‌شده:

- Active Demo Time
- Current Stage
- Progress Credits
- Care Mask هر Stage
- Completion Flag

## کانال‌های انتشار

پروژه از دو کانال انتشار استفاده می‌کند:

```text
Stable      -> main    -> vX.Y.Z
Pre-release -> develop -> vX.Y.Z-rc.N
```

هر تغییر نهایی‌شده روی یکی از شاخه‌های انتشار باید هم‌زمان شماره نسخه جدید، `VERSION`، `CHANGELOG.md` و شماره نسخه داخل Firmware را به‌روزرسانی کند.

Push روی `main` یک Release پایدار می‌سازد و Push نسخه‌دار روی `develop` یک GitHub Pre-release می‌سازد. Tag و Release منتشرشده هرگز بازنویسی نمی‌شوند.

## روند توسعه


از این مخزن به‌عنوان مرجع اصلی Firmware استفاده می‌کنیم.

در تغییرهای بعدی:

1. آخرین نسخه موجود در مخزن مبنا است.
2. تغییرها ابتدا روی همان نسخه اعمال می‌شوند.
3. نسخه جدید با توضیح مشخص Commit می‌شود.
4. تغییرهای سخت‌افزاری و Pin Map در همین README نیز ثبت می‌شوند.
