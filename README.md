# تندو (Tando)

تندو یک ربات/اسباب‌بازی تعاملی بر پایه ESP32-S3 است که با دو نمایشگر گرد، سنسور لمسی خازنی، RFID و LED به رفتار کاربر واکنش نشان می‌دهد.

این مخزن برای توسعه Firmware نسخه دموی ۳۰ دقیقه‌ای تندو استفاده می‌شود.

## وضعیت فعلی

نسخه Pre-release فعلی Firmware: **v0.10.0-rc.1**

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

در `v0.9.0-rc.2` استیکر زرد قبلی به‌طور کامل حذف شده است. Hunger دیگر جای چشم را نمی‌گیرد.

در زمان Hunger:

```text
چشم چپ و راست
→ همان Renderer و State Machine عادی خود را ادامه می‌دهند

پایین نمایشگر چپ
→ ران مرغ متحرک

پایین نمایشگر راست
→ همان ران مرغ متحرک
```

یعنی Look Around، Blink، Wink، Eye Smile، Play و Reactionهای عادی چشم می‌توانند هم‌زمان با Food cue پایین نمایشگر ادامه پیدا کنند.

Hunger Scheduler به `demoStarted` یا `demoClockRunning` وابسته نیست. از زمان Boot، حتی اگر هنوز هیچ Interaction انجام نشده باشد و Status مقدار `Demo started: NO` را نشان دهد، Hunger برای Stage فعلی Schedule می‌شود. همچنین Pause شدن Active Demo Time به‌دلیل بی‌تعامل‌بودن، Hunger را متوقف نمی‌کند. این رفتار عمدی است چون Hunger جزو رفتارهای زمان Idle است.

در هر Stage ده‌دقیقه‌ای، تا وقتی FOOD همان Stage ثبت نشده باشد:

```text
حداکثر 15 Hunger Request
هر Request = دقیقاً 10 ثانیه
زمان شروع = Random
```

زمان‌بندی فعلی طوری محدود شده که در حالت بدون وقفه هر 15 فرصت داخل ده دقیقه جا شوند:

```text
اولین درخواست: 8–20 s
درخواست‌های بعدی پس از تکمیل: 8–18 s
Retry پس از قطع به‌دلیل Sleep/System: 5–10 s

Care Request scheduler مشترک با Pet Request:
هر دو Need هر کدام 15×10s هستند و روی هم نمی‌افتند.
```

PET و رفتارهای عمومی چشم Hunger overlay را قطع نمی‌کنند؛ چشم‌ها واکنش خود را انجام می‌دهند و ران مرغ در پایین هر دو نمایشگر باقی می‌ماند. Sleep و رویدادهای System مثل Stage Unlock/Completion کل صورت را در اختیار می‌گیرند؛ اگر Hunger به این دلیل قطع شود، آن نوبت مصرف نمی‌شود و بعداً Retry می‌شود.

به‌محض اولین FOOD معتبر در همان Stage:

```text
FOOD
→ Hunger overlay فوری قطع
→ FOOD Progress طبق قانون Stage ثبت
→ تمام Hunger Requestهای باقی‌مانده همان Stage لغو
```

با ورود به Stage بعدی شمارنده Hunger صفر می‌شود و Need غذا دوباره فعال است.

Count Requestهای کامل‌شده با همان کلیدهای additive در NVS ذخیره می‌شود؛ `NVS_STATE_VERSION` همچنان 4 باقی مانده است. اگر برد از `v0.9.0-rc.1` دارای Count ذخیره‌شده باشد، همان Count ادامه پیدا می‌کند و سقف جدید 15 اعمال می‌شود.

Serial command `h` فقط Preview ده‌ثانیه‌ای ران مرغ روی هر دو نمایشگر است و سهم 15تایی Stage را مصرف نمی‌کند.


## Pet Request / درخواست نوازش

از `v0.10.0-rc.1` تندو علاوه بر Hunger، یک Need مستقل برای درخواست نوازش دارد.

در هر Stage، تا زمانی که PET همان Stage ثبت نشده باشد:

```text
حداکثر 15 Pet Request
هر Request = دقیقاً 10 ثانیه
زمان شروع = Random
```

Pet Request مانند Hunger با wall-clock اجرا می‌شود؛ یعنی قبل از اولین Interaction و هنگام Pause بودن Active Demo Time نیز فعال است، اما خودش Demo clock را Start/Resume نمی‌کند.

Visual درخواست نوازش عمداً از PET Reaction واقعی ضعیف‌تر است:

```text
چشم‌ها باقی می‌مانند
→ نگاه نرم کمی بالا و به داخل
→ eyelid soften بسیار کم
→ blush ملایم

پایین هر دو نمایشگر
→ آیکون دست ساده
→ حرکت آرام چپ/راست شبیه نوازش
```

Heart بزرگ یا Sticker تمام‌صفحه استفاده نشده است. وقتی PET واقعی رخ می‌دهد، Hand cue فوراً حذف می‌شود و PET Reaction اصلی با نگاه بالاتر، Glow/Blush/Sparkle قوی‌تر اجرا می‌شود؛ بنابراین «درخواست» و «پاداش لمس واقعی» از هم قابل تشخیص هستند.

اولین PET معتبر در همان Stage:

```text
MPR121 live 2-of-3 PET
→ Pet Request فوری قطع
→ PET Progress طبق قانون Stage ثبت
→ تمام Pet Requestهای باقی‌مانده همان Stage لغو
→ PET Reaction واقعی اجرا
```

در Stage بعد Pet Request count صفر می‌شود و Need نوازش دوباره فعال است.

Hunger و Pet Request هیچ‌وقت هم‌زمان نمایش داده نمی‌شوند. اگر هر دو در یک لحظه Due باشند، Firmware به‌صورت Random یکی را برای Slot فعلی انتخاب می‌کند و دیگری چند ثانیه Defer می‌شود.

برای اینکه در صورت برطرف‌نشدن هیچ‌کدام از Needها، 15 Hunger + 15 Pet Request در یک Stage ده‌دقیقه‌ای قابل ارائه باشند، Care Request timing مشترک به این شکل است:

```text
اولین Request هر Need: 8–20 s
Requestهای بعدی: 8–18 s
Retry بعد از interruption: 5–10 s
Collision defer: 3–8 s
```

Serial command `r` یک Preview ده‌ثانیه‌ای Pet Request است و Stage count یا Progress را تغییر نمی‌دهد.

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
