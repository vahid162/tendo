# تندو (Tando)

تندو یک ربات/اسباب‌بازی تعاملی بر پایه ESP32-S3 است که با دو نمایشگر گرد، سنسور لمسی خازنی، RFID و LED به رفتار کاربر واکنش نشان می‌دهد.

این مخزن برای توسعه Firmware نسخه دموی ۱۵ دقیقه‌ای تندو استفاده می‌شود.

## وضعیت فعلی

نسخه مرجع فعلی Firmware: **v6**

فایل اصلی Firmware:

```text
tando_final_demo_15min.ino
```

از این فایل به‌عنوان مبنای تغییرهای بعدی استفاده می‌کنیم.

ویژگی‌های فعال:

- دو چشم روی دو نمایشگر GC9A01 با رزولوشن 240×240
- حرکت زنده و بازیگوش چشم‌ها در حالت Idle
- پلک‌زدن خودکار
- واکنش نوازش با MPR121
- تشخیص PET با حداقل ۲ الکترود از E0 / E1 / E2
- حداقل زمان تماس واقعی برای PET: حدود ۲ ثانیه
- حساسیت فعلی MPR121:
  - Touch Threshold = 6
  - Release Threshold = 3
- دو تگ FOOD
- یک تگ SLEEP
- خواب پایدار وابسته به حضور SLEEP TAG
- حلقه Progress متقارن دور هر دو چشم
- سه Stage پنج‌دقیقه‌ای
- مجموع زمان Demo برابر ۱۵ دقیقه Active Time
- ذخیره Stage، Progress و زمان Demo در NVS
- PWM برای LED واکنش روی GPIO21

## منطق Demo

### Stage 1
از دقیقه 0 تا 5

### Stage 2
از دقیقه 5 تا 10

### Stage 3
از دقیقه 10 تا 15

### Completion
در پایان 15 دقیقه:

- Progress روی 100٪ قرار می‌گیرد.
- Stage روی 3 باقی می‌ماند.
- تعامل‌ها همچنان واکنش دارند.
- Progress دیگر افزایش پیدا نمی‌کند.

در هر Stage فقط یک Credit برای هر کلاس مراقبت ثبت می‌شود:

- PET
- FOOD
- SLEEP

در نتیجه هر Stage حداکثر 3 Credit دارد و کل Demo برابر 9 Credit است.

## رفتار PET / نوازش

برای فعال شدن PET:

- حداقل دو الکترود متفاوت از E0، E1 و E2 باید در یک Session لمس شوند.
- ترتیب لمس مهم نیست.
- مجموع زمان واقعی تماس خازنی باید حداقل حدود 2 ثانیه باشد.
- فاصله کوتاه هنگام حرکت دست بین الکترودها مجاز است.
- بعد از Trigger، سیستم تا آزاد شدن کامل Touch دوباره PET را ثبت نمی‌کند.

نمونه:

```text
E0 + E1 -> PET
E0 + E2 -> PET
E1 + E2 -> PET
E0 + E1 + E2 -> PET
```

یک الکترود به‌تنهایی PET ایجاد نمی‌کند.

واکنش چشم در PET:

- نگاه واضح به بالا
- نگاه کمی به داخل
- حالت مهربان‌تر
- Glow نرم
- بازگشت نرم به Idle

## خواب

SLEEP یک State واقعی است:

```text
SLEEP TAG PRESENT -> ASLEEP
SLEEP TAG REMOVED -> WAKE UP
```

تا زمانی که SLEEP TAG جلوی RC522 باشد، چشم‌ها در خواب می‌مانند.

بعد از حذف کامل Tag، چشم‌ها آرام باز می‌شوند.

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

> نکته مهم: LED فیلامنت 3V / 120mA نباید مستقیم از GPIO21 تغذیه شود. GPIO21 فقط باید Gate یک MOSFET Logic-Level را کنترل کند. GND منبع LED و ESP32 باید مشترک باشد.

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
t = نمایش Diagnostic خام E0/E1/E2 در MPR121
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

خاموش بودن دستگاه جزو 15 دقیقه Demo حساب نمی‌شود.

## حافظه

برای ذخیره وضعیت از Preferences / NVS داخلی ESP32 استفاده می‌شود.

موارد مهم ذخیره‌شده:

- Active Demo Time
- Current Stage
- Progress Credits
- Care Mask هر Stage
- Completion Flag

## روند توسعه

از این مخزن به‌عنوان مرجع اصلی Firmware استفاده می‌کنیم.

در تغییرهای بعدی:

1. آخرین نسخه موجود در مخزن مبنا است.
2. تغییرها ابتدا روی همان نسخه اعمال می‌شوند.
3. نسخه جدید با توضیح مشخص Commit می‌شود.
4. تغییرهای سخت‌افزاری و Pin Map در همین README نیز ثبت می‌شوند.
