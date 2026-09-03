# تندو (Tando)

تندو یک ربات/اسباب‌بازی تعاملی بر پایه ESP32-S3 است که با دو نمایشگر گرد، سنسور لمسی خازنی، RFID و LED به رفتار کاربر واکنش نشان می‌دهد.

این مخزن برای توسعه Firmware نسخه دموی ۳۰ دقیقه‌ای تندو استفاده می‌شود.

## وضعیت فعلی

نسخه Pre-release فعلی Firmware: **v0.10.0-rc.8**

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
- تأیید PET بعد از رسیدن Live Touch Count به حداقل ۲ الکترود: فقط **20 ms پایداری**؛ شرط تجمع حدود ۱ ثانیه حذف شده است
- State Manager جدید برای جلوگیری از گم‌شدن واکنش‌ها هنگام اجرای انیمیشن دیگر
- غیرفعال شدن PET در حالت Sleep
- صف مستقل برای Stage 2 / Stage 3 / Completion
- حساسیت فعلی MPR121:
  - Touch Threshold = 6
  - Release Threshold = 3
- دو تگ FOOD
- یک تگ SLEEP
- خواب پایدار وابسته به حضور SLEEP TAG
- Credit خواب با Active Demo Time محلی هر Stage: از 06:00 و Sleep Request فقط از 09:00
- حلقه Progress متقارن دور هر دو چشم
- سه Stage ده‌دقیقه‌ای
- مجموع زمان Demo برابر ۳۰ دقیقه Active Time
- لایه شخصیت خودکار عمومی با چهار خانواده رفتار: Look Around / Wink / Eye Smile / Play Invite
- زمان‌بندی نامنظم رویدادهای شخصیت عمومی در سه کلاس 8–20، 20–55 و 55–120 ثانیه
- انتخاب وزن‌دار با Jitter، کاهش احتمال تکرار ۲–۳ رفتار اخیر و افزایش زمینه‌ای احتمال Play Invite در سکوت طولانی‌تر
- Hunger و Pet Request عضو Pool عمومی Random نیستند و با Scheduler مستقل Stage-aware اجرا می‌شوند
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

- Completion اجرا می‌شود، اما Progress روی تعداد Creditهای واقعی/مجاز همان Demo ثابت می‌ماند.
- Stage روی 3 باقی می‌ماند.
- تعامل‌ها همچنان واکنش دارند.
- Progress دیگر افزایش پیدا نمی‌کند.

در هر Stage فقط یک Credit برای هر کلاس مراقبت ثبت می‌شود:

- PET
- FOOD
- SLEEP

در نتیجه هر Stage حداکثر 3 Credit دارد و کل Demo برابر 9 Credit است.

PET و FOOD می‌توانند طبق سیاست نمایشی موجود در مرز Stage auto-fill شوند؛ اما **SLEEP هرگز auto-fill نمی‌شود**. بنابراین اگر Sleep معتبر یک Stage انجام نشده باشد، Progress نهایی الزاماً 9/9 یا 100٪ نخواهد بود.


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

Stage-aware Care Requests
→ Hunger: 10-second chicken-drumstick overlay on both displays, up to 10 completed automatic prompts per Stage until FOOD
→ Pet Request: 10-second pulsing ((heart)) alert on both displays, up to 10 completed automatic prompts per Stage until PET
```

رویدادهای شخصیت خودکار عمومی هیچ Credit، Progress یا LED interaction pulse ایجاد نمی‌کنند و Active Demo Time را نیز Resume نمی‌کنند. هر تعامل واقعی کاربر یا رویداد System، Visual خودکار جاری را حذف می‌کند؛ رفتار خودکار قدیمی بعد از Reaction در صف پخش نمی‌شود و Scheduler با یک Delay تصادفی جدید شروع می‌شود.

Hunger و Pet Request از این Pool جدا هستند و طبق سناریوی مراقبتی Stage اجرا می‌شوند.

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

از `v0.10.0-rc.4`، Hunger و Pet Request روی یک **Stage-local wall-clock timeline** اجرا می‌شوند. علت این انتخاب این است که Need Request باید حتی وقتی کودک برای مدتی با تندو تعامل نمی‌کند دیده شود؛ Pause شدن Active Demo Time نباید خود Requestهای نیاز را خاموش کند.

```text
بعد از Boot / در Stage فعلی   → Care Scheduler فعال است
Demo clock RUNNING            → Requestها طبق wall-clock ادامه دارند
Demo clock PAUSED             → Requestها باز هم طبق wall-clock ادامه دارند
```

هر Stage برای Care Scheduler ده Slot یک‌دقیقه‌ای دارد. در هر Slot یک Need در پنجره Early و دیگری در پنجره Late قرار می‌گیرد:

```text
Early window = ثانیه 5 تا 15 همان دقیقه، Random
Late window  = ثانیه 35 تا 45 همان دقیقه، Random
Request      = 10 ثانیه
```

Hunger و Pet Request به‌صورت متناوب Early/Late را می‌گیرند تا روی هم نیفتند و تا سقف 10 بار برای هر Need روی حدود 10 دقیقه پخش شوند. اگر یک پنجره **در همان Boot** به‌علت Reaction/Sleep یا تأخیر Runtime طولانی stale شود، آن Slot Skip می‌شود و Requestهای قدیمی پشت‌سرهم پخش نمی‌شوند.

نکته Reboot: موقعیت wall-clock فعلی Care Scheduler در NVS ذخیره نمی‌شود. بعد از Boot جدید، anchor زمان‌بندی از همان Session جدید شروع می‌شود؛ فقط Stage و Countهای ذخیره‌شده Hunger/Pet Request بازیابی می‌شوند. بنابراین Firmware موقعیت wall-clock قبل از خاموشی را بازسازی نمی‌کند.

اگر Request خودکار با Reaction واقعی قطع شود، Count مصرف نمی‌شود و Retry بعد از پایان Reaction با 5–10 ثانیه wall-clock delay انجام می‌شود. بعد از پایان طبیعی هر Care Request نیز حداقل 5 ثانیه wall-clock cooldown وجود دارد.

**Manual Preview مستقل است:** فرمان `h` حتی اگر FOOD همان Stage قبلاً ثبت شده باشد و حتی اگر Demo clock Pause باشد، Hunger را دقیقاً 10 ثانیه Preview می‌کند. فرمان `r` نیز مستقل از PET satisfaction، quota و Demo clock، Pet Request را 10 ثانیه Preview می‌کند. تنها یک Reaction واقعی/Queued می‌تواند Preview را زودتر قطع کند.

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
- `i` = Status؛ علاوه بر Count و Slot، وضعیت `RUNNING / STOPPED` و در صورت Schedule شدن `nextIn=<seconds>` را نشان می‌دهد.

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

Interaction Manager رویدادهای کاربر را هنگام اجرای انیمیشن‌های کوتاه گم نمی‌کند؛ رویدادهای قابل‌صف به‌صورت Pending/Coalesced نگه‌داری می‌شوند تا صفی از انیمیشن‌های قدیمی ساخته نشود.

مدل فعلی اولویت و نمایش:

```text
Persistent Sleep (وقتی فعال است) = Visual Lock مطلق
        ↓ بعد از Wake
Pending System: Completion / Stage Unlock
        ↓
Real user reactions: Food / Pet / Unknown RFID
        ↓
Sleep request
        ↓
Care Request
        ↓
Autonomous Personality
        ↓
Micro Idle
```

وقتی هر Reaction واقعی/System فعال است، فقط همان Reaction روی TFT دیده می‌شود؛ Hunger/Pet Request، Autonomous، Progress Ring و Ambient Blink هم‌زمان Render نمی‌شوند. Blink عمدی داخل FOOD بخشی از خود FOOD Reaction است.

اگر PET یا FOOD هنگام اجرای یک Reaction کوتاه دیگر برسد، رویداد می‌تواند Pending/Coalesced شود و بعد از آزادشدن اولویت مناسب نمایش داده شود. بین Reactionهای کاربر یک handoff کوتاه حدود 240 ms وجود دارد تا Blendهای حالت قبلی، مثل Happy/Surprise/Glow، وارد Reaction بعدی نشوند.

**Sleep استثنای اصلی اولویت بصری است:** وقتی `R_SLEEP` فعال است، Stage Unlock و Completion فقط Pending می‌شوند و حق Preempt کردن Sleep را ندارند. بعد از برداشتن SLEEP TAG و پایان Wake، System eventهای Pending سرویس می‌شوند.

RFID هنگام اجرای PET/FOOD همچنان Poll می‌شود و برای چند ثانیه کور نمی‌شود.

## خواب

SLEEP یک State واقعی است:

```text
SLEEP TAG PRESENT -> ASLEEP
SLEEP TAG REMOVED -> WAKE UP
```

تا زمانی که SLEEP TAG جلوی RC522 باشد، چشم‌ها در خواب می‌مانند.

بعد از حذف کامل Tag، چشم‌ها آرام باز می‌شوند. زمان Auto Blink بعد از Wake دوباره زمان‌بندی می‌شود تا بلافاصله بعد از بازشدن چشم، Blink ناخواسته اجرا نشود.

### Credit خواب و Sleep Request در v0.10.0-rc.8

هر Stage ده دقیقه **Active Demo Time** دارد. زمان‌بندی Credit و Request خواب فقط از همین زمان استفاده می‌کند؛ Schedulerهای Hunger و Pet Request همچنان wall-clock محلی Stage خود را دارند.

```text
00:00 تا 05:59  → SLEEP واقعی و پایدار فعال است، اما +0 Progress
06:00 تا 08:59  → اولین SLEEP واقعی همان Stage دقیقاً +1 Credit می‌گیرد
09:00 تا 09:59  → همان Credit هنوز معتبر است؛ فقط اگر Credit نگرفته باشد Sleep Request نمایش داده می‌شود
```

هر Stage فقط یک `CARE_SLEEP_BIT` مستقل دارد. اگر SLEEP پیش از 06:00 شروع شود و Tag تا عبور از 06:00 باقی بماند، همان خواب فعال بدون نیاز به برداشتن و ارائهٔ دوبارهٔ Tag دقیقاً یک Credit می‌گیرد. SLEEPهای تکراری همیشه حالت خواب/LED طبیعی را دارند، ولی بعد از ثبت Credit همان Stage دیگر Progress اضافه نمی‌کنند.

در دقیقهٔ 09:00، اگر Credit خواب Stage هنوز ثبت نشده باشد، Sleep Request پایدار arm می‌شود: هر دو چشم باز می‌مانند، پلک بالایی حدود 35–55٪ سنگین است، حرکت و نگاه آرام و رو به پایین/مرکز است و Zهای ظریف دیده می‌شوند. این حالت با `R_SLEEP` واقعی فرق دارد و Progress، LED pulse، User Activity یا Resume شدن Active Demo Time ایجاد نمی‌کند.

Reaction کوتاه واقعی می‌تواند Sleep Request را موقتاً بپوشاند؛ اگر Sleep هنوز برآورده نشده باشد، Request بعد از Reaction بازمی‌گردد. ارائهٔ SLEEP Tag در این وضعیت، Request را رفع می‌کند، `R_SLEEP` واقعی را شروع می‌کند و دقیقاً یک Credit می‌دهد.

## اصلاحات حرکت چشم در v0.7.2-rc.8

- Blink عادی مانند Sleep به‌صورت Top-Lid-Dominant رسم می‌شود و دیگر Eye Shape از بالا و پایین به مرکز جمع نمی‌شود.
- Unknown RFID به‌جای Shake سریع و مخالف دو چشم، یک نگاه کنجکاوانه و آرام‌تر دارد.
- حرکت ریز Food در فاز خوردن کندتر شده است تا بیشتر شبیه chewing باشد و کمتر شبیه jitter دیده شود.
- فاصله تغییر Targetهای Idle در Stage 2 و Stage 3 افزایش یافته است تا شخصیت چشم بی‌قرار نشود.
- رنگ Progress Ring هنگام `R_UNLOCK2` و `R_UNLOCK3` از خود Reaction گرفته می‌شود تا Unlock قدیمی با رنگ Stage جدید نمایش داده نشود.
- **رفتار تاریخی rc.8:** در آن نسخه Completion/Stage Unlock می‌توانستند Sleep را قطع کنند. این رفتار دیگر Current Contract نیست؛ در نسخه فعلی Sleep Visual Lock مطلق است و System eventها تا بعد از Wake Pending می‌مانند.

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

l = Preview/اجرای Autonomous LOOK
w = Preview/اجرای Autonomous WINK
e = Preview/اجرای Autonomous EYE SMILE
g = Preview/اجرای Autonomous PLAY INVITE

h = Preview ده‌ثانیه‌ای Hunger Drumstick، بدون Stage Count/Progress
r = Preview ده‌ثانیه‌ای Pet Request ((HEART))، بدون Stage Count/Progress

b = Blink؛ هنگام Reaction واقعی یا Sleep Request Block می‌شود
i = نمایش وضعیت Demo و Care Scheduler/nextIn، شامل زمان Active محلی Stage و وضعیت Sleep
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

برای Sleep، آستانه‌های 06:00 و 09:00 نیز فقط با همین Active Demo Time محلی Stage سنجیده می‌شوند؛ `millis()` دیواری و Pauseهای بدون تعامل نباید آن‌ها را جلو ببرند.

## حافظه

برای ذخیره وضعیت از Preferences / NVS داخلی ESP32 استفاده می‌شود.

در `v0.10.0-rc.8` نسخه State داخلی NVS از 4 به 5 افزایش یافته است. Stateهای v4 در نخستین Boot این نسخه Reset می‌شوند، چون نسخهٔ قدیمی می‌توانست Sleep Credit را در مرز Stage/Completion به‌صورت مصنوعی بسازد و دیگر نمی‌توان آن داده را با قرارداد «فقط Sleep واقعی در پنجرهٔ معتبر» بدون ابهام تفسیر کرد.

موارد مهم ذخیره‌شده:

- Active Demo Time (`elapsed`)
- Current Stage (`stage`)
- Progress Credits (`credits`)
- Care Mask **Stage جاری** (`mask`)
- Demo Started Flag (`started`)
- Completion Flag (`done`)
- Hunger Request Stage / completed Count (`hStage` / `hCount`)
- Pet Request Stage / completed Count (`pStage` / `pCount`)

موارد Runtime-only مثل `nextHungerRequestAt`، `nextPetRequestAt`، Care wall-clock anchor، `sleepRequestPending` و Promptهای فعال در NVS ذخیره نمی‌شوند. بعد از Reboot، Active Demo Clock تا Interaction بعدی Pause می‌ماند؛ Care Scheduler anchor نیز برای Session جدید از نو ساخته می‌شود. Sleep Request در صورت رسیدن Active Demo Time به 09:00 و نبود Credit خواب، دوباره از state ذخیره‌شده arm می‌شود.

## کانال‌های انتشار

پروژه از دو کانال انتشار استفاده می‌کند:

```text
Stable      -> main    -> vX.Y.Z
Pre-release -> develop -> vX.Y.Z-rc.N
```

هر تغییر نهایی‌شده روی یکی از شاخه‌های انتشار باید هم‌زمان شماره نسخه جدید، `VERSION`، `CHANGELOG.md` و شماره نسخه داخل Firmware را به‌روزرسانی کند.

Push روی `main` یک Release پایدار می‌سازد و Push نسخه‌دار روی `develop` یک GitHub Pre-release می‌سازد. Tag و Release منتشرشده هرگز بازنویسی نمی‌شوند.

## روند توسعه


از این مخزن به‌عنوان مرجع اصلی Firmware استفاده می‌کنیم. برای توسعه عادی، `develop` مرجع Current Pre-release است و `main` مرجع Stable باقی می‌ماند.

در تغییرهای بعدی:

1. آخرین `develop` برای توسعه عادی مبنا است، مگر اینکه Stable promotion یا repository repair صریحاً درخواست شود.
2. تغییرها روی Branch مناسب و با حفظ رفتارهای نامرتبط اعمال می‌شوند.
3. نسخه جدید و CHANGELOG مطابق Release policy به‌روزرسانی می‌شوند.
4. تغییرهای سخت‌افزاری، Pin Map، UID، PET/Sleep/Timing و Serial commands در همین README نیز همگام می‌شوند.
