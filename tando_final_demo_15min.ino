#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Preferences.h>
#include <math.h>
#include <esp_system.h>
#include <esp_arduino_version.h>

#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <Adafruit_MPR121.h>
#include <MFRC522.h>

#define TANDO_VERSION_MAJOR 0
#define TANDO_VERSION_MINOR 10
#define TANDO_VERSION_PATCH 0
#define TANDO_VERSION "0.10.0-rc.1"


// ============================================================
// TANDO - FINAL 30 MIN DEMO FIRMWARE
// Firmware v0.10.0-rc.1: Stage-aware PET requests + coordinated care-request scheduler
// ESP32-S3 + 2x GC9A01 + MPR121 + RC522 + 1 PWM LED
//
// Demo:
//   Stage 1 : active minute 0..10
//   Stage 2 : active minute 10..20
//   Stage 3 : active minute 20..30
//   Complete: active minute 30
//
// Each stage can earn at most 3 care credits:
//   PET once + FOOD once + SLEEP once
// Extra interactions still get eye + LED feedback, but +0 progress.
// Missing visual progress is auto-filled at each 10-minute boundary so the
// presentation is guaranteed to reach 100% in 30 active demo minutes.
//
// Active demo time pauses after 60 seconds with no user interaction.
// Power-off time is NOT counted. State is restored from ESP32 NVS.
// ============================================================

// ============================================================
// PIN MAP
// ============================================================

// Eyes - separate hardware SPI
#define TFT_SCLK    4
#define TFT_MOSI    5
#define LEFT_DC     6
#define LEFT_CS     7
#define LEFT_RST    15
#define RIGHT_DC    18
#define RIGHT_CS    16
#define RIGHT_RST   17

// MPR121
#define MPR_SDA     8
#define MPR_SCL     9

// RC522 - default SPI
#define RFID_SCK    10
#define RFID_MISO   11
#define RFID_MOSI   12
#define RFID_SS     13
#define RFID_RST    14

// Low-current reaction LED.
// Hardware: GPIO21 -> suitable series resistor -> LED -> GND.
// Keep LED current within the ESP32-S3 GPIO limit.
#define REACTION_LED_PIN 21

// ============================================================
// DEVICES
// ============================================================

SPIClass displaySPI(HSPI);

Adafruit_GC9A01A leftDisplay(
  &displaySPI,
  LEFT_DC,
  LEFT_CS,
  LEFT_RST
);

Adafruit_GC9A01A rightDisplay(
  &displaySPI,
  RIGHT_DC,
  RIGHT_CS,
  RIGHT_RST
);

Adafruit_MPR121 mpr;
MFRC522 rfid(RFID_SS, RFID_RST);
Preferences prefs;

// ============================================================
// RFID TAGS
// ============================================================

// FOOD 1
const byte FOOD1_UID[] = { 0x96, 0x2B, 0xCD, 0xAB };
const byte FOOD1_UID_SIZE = sizeof(FOOD1_UID);

// FOOD 2
const byte FOOD2_UID[] = { 0xF6, 0x33, 0x11, 0xAA };
const byte FOOD2_UID_SIZE = sizeof(FOOD2_UID);

// SLEEP
const byte SLEEP_UID[] = { 0xC6, 0x34, 0xBD, 0xAA };
const byte SLEEP_UID_SIZE = sizeof(SLEEP_UID);

// ============================================================
// RENDER CANVAS
// 200x200 leaves room for a thin progress ring.
// One canvas is reused for both physical displays.
// ============================================================

#define FRAME_W 200
#define FRAME_H 200
#define FRAME_X 20
#define FRAME_Y 20

GFXcanvas16 frame(FRAME_W, FRAME_H);

const float SCREEN_CX = 100.0f;
const float SCREEN_CY = 100.0f;
const float EYE_BASE_Y = 96.0f;

// ============================================================
// COLORS
// ============================================================

uint16_t C_BLACK;
uint16_t C_WHITE;
uint16_t C_EYE;
uint16_t C_EYE_GLOW;
uint16_t C_EYE_SHINE;
uint16_t C_BLUSH;
uint16_t C_RING_TRACK;
uint16_t C_RING_S1;
uint16_t C_RING_S2;
uint16_t C_RING_S3;
uint16_t C_SLEEP_Z;
uint16_t C_SPARK;

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) |
         ((g & 0xFC) << 3) |
         (b >> 3);
}

uint16_t blend565(uint16_t a, uint16_t b, float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;

  int ar = (a >> 11) & 0x1F;
  int ag = (a >> 5)  & 0x3F;
  int ab = a & 0x1F;

  int br = (b >> 11) & 0x1F;
  int bg = (b >> 5)  & 0x3F;
  int bb = b & 0x1F;

  int r = ar + (int)((br - ar) * t);
  int g = ag + (int)((bg - ag) * t);
  int bl = ab + (int)((bb - ab) * t);

  return (r << 11) | (g << 5) | bl;
}

// ============================================================
// MATH HELPERS
// ============================================================

float clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

float smoothStep(float t) {
  t = clamp01(t);
  return t * t * (3.0f - 2.0f * t);
}

float smoothFollow(float current, float target, float dt, float speed) {
  float alpha = 1.0f - expf(-speed * dt);
  return current + (target - current) * alpha;
}

float mapF(float value, float inMin, float inMax, float outMin, float outMax) {
  if (inMax == inMin) return outMin;
  float t = (value - inMin) / (inMax - inMin);
  return outMin + (outMax - outMin) * t;
}

// ============================================================
// DEMO / PROGRESS CONSTANTS
// ============================================================

const uint32_t STAGE_MS = 10UL * 60UL * 1000UL;
const uint32_t TOTAL_DEMO_MS = 30UL * 60UL * 1000UL;

// Pause active-demo clock after 60 seconds without user interaction.
const uint32_t INACTIVITY_PAUSE_MS = 60UL * 1000UL;

// NVS timer checkpoint. Keeps flash writes low.
const uint32_t NVS_CHECKPOINT_MS = 15UL * 1000UL;

const uint8_t TOTAL_VISUAL_CREDITS = 9;

// Care bits inside the CURRENT stage.
const uint8_t CARE_PET_BIT   = 0x01;
const uint8_t CARE_FOOD_BIT  = 0x02;
const uint8_t CARE_SLEEP_BIT = 0x04;

// ============================================================
// PERSISTENT DEMO STATE
// ============================================================

// Timing semantics changed from the 15-minute demo to the 30-minute demo.
// Reset old persisted demo state once instead of silently reinterpreting it.
const uint32_t NVS_STATE_VERSION = 4;

bool demoStarted = false;
bool demoClockRunning = false;
bool completionFlag = false;

uint32_t savedActiveMs = 0;
uint32_t activeRunStartMs = 0;
uint32_t lastInteractionMs = 0;
uint32_t lastNvsCheckpointElapsed = 0;

uint8_t currentStage = 1;      // 1..3
uint8_t stageCareMask = 0;     // PET/FOOD/SLEEP already credited in current stage
uint8_t visualCredits = 0;     // 0..9, controls ring

// Hunger request persistence. These are additive keys; old v4 demo state
// remains valid and simply loads these fields from defaults.
uint8_t hungerRequestStage = 0;
uint8_t hungerRequestsShown = 0;

// Runtime-only Hunger request scheduler state.
// Hunger is now an overlay; the normal eye state machine keeps running under it.
// This scheduler intentionally uses wall-clock millis(), not Active Demo Time:
// it runs while Tando is idle, before the first interaction, and while the Demo
// clock is paused. The current Stage still determines the per-Stage quota.
uint32_t nextHungerRequestAt = 0;
bool hungerPromptActive = false;
bool hungerPromptTracked = false;
bool hungerPromptManualPreview = false;
uint32_t hungerPromptStart = 0;
bool hungerRetryPending = false;

// PET Request persistence mirrors Hunger through additive NVS keys.
uint8_t petRequestStage = 0;
uint8_t petRequestsShown = 0;

// Runtime-only PET Request scheduler state.
// PET Request is also wall-clock/idle behavior and does not start/resume
// Active Demo Time. It never changes the capacitive PET detector itself.
uint32_t nextPetRequestAt = 0;
bool petRequestPromptActive = false;
bool petRequestPromptTracked = false;
bool petRequestPromptManualPreview = false;
uint32_t petRequestPromptStart = 0;
bool petRequestRetryPending = false;

const uint8_t PENDING_UNLOCK_STAGE2 = 0x01;
const uint8_t PENDING_UNLOCK_STAGE3 = 0x02;
uint8_t pendingUnlockMask = 0;
bool pendingCompletion = false;

// ============================================================
// DEMO CLOCK / NVS
// ============================================================

uint32_t getActiveElapsedMs(uint32_t now) {
  if (completionFlag) {
    return TOTAL_DEMO_MS;
  }

  uint32_t elapsed = savedActiveMs;

  if (demoClockRunning) {
    elapsed += (now - activeRunStartMs);
  }

  if (elapsed > TOTAL_DEMO_MS) {
    elapsed = TOTAL_DEMO_MS;
  }

  return elapsed;
}

void savePersistentState(uint32_t now) {
  uint32_t elapsed = getActiveElapsedMs(now);

  prefs.putUInt("ver", NVS_STATE_VERSION);
  prefs.putUInt("elapsed", elapsed);
  prefs.putUChar("stage", currentStage);
  prefs.putUChar("credits", visualCredits);
  prefs.putUChar("mask", stageCareMask);
  prefs.putBool("started", demoStarted);
  prefs.putBool("done", completionFlag);
  prefs.putUChar("hStage", hungerRequestStage);
  prefs.putUChar("hCount", hungerRequestsShown);
  prefs.putUChar("pStage", petRequestStage);
  prefs.putUChar("pCount", petRequestsShown);
  lastNvsCheckpointElapsed = elapsed;
}

void loadPersistentState() {
  uint32_t version = prefs.getUInt("ver", 0);

  if (version != NVS_STATE_VERSION) {
    prefs.clear();
    prefs.putUInt("ver", NVS_STATE_VERSION);

    demoStarted = false;
    completionFlag = false;
    savedActiveMs = 0;
    currentStage = 1;
    visualCredits = 0;
    stageCareMask = 0;
    hungerRequestStage = 0;
    hungerRequestsShown = 0;
    nextHungerRequestAt = 0;
    hungerPromptActive = false;
    hungerPromptTracked = false;
    hungerPromptManualPreview = false;
    hungerPromptStart = 0;
    hungerRetryPending = false;
    petRequestStage = 0;
    petRequestsShown = 0;
    nextPetRequestAt = 0;
    petRequestPromptActive = false;
    petRequestPromptTracked = false;
    petRequestPromptManualPreview = false;
    petRequestPromptStart = 0;
    petRequestRetryPending = false;
    return;
  }

  demoStarted = prefs.getBool("started", false);
  completionFlag = prefs.getBool("done", false);
  savedActiveMs = prefs.getUInt("elapsed", 0);
  currentStage = prefs.getUChar("stage", 1);
  visualCredits = prefs.getUChar("credits", 0);
  stageCareMask = prefs.getUChar("mask", 0);
  hungerRequestStage = prefs.getUChar("hStage", 0);
  hungerRequestsShown = prefs.getUChar("hCount", 0);
  petRequestStage = prefs.getUChar("pStage", 0);
  petRequestsShown = prefs.getUChar("pCount", 0);
  nextHungerRequestAt = 0;
  hungerPromptActive = false;
  hungerPromptTracked = false;
  hungerPromptManualPreview = false;
  hungerPromptStart = 0;
  hungerRetryPending = false;
  nextPetRequestAt = 0;
  petRequestPromptActive = false;
  petRequestPromptTracked = false;
  petRequestPromptManualPreview = false;
  petRequestPromptStart = 0;
  petRequestRetryPending = false;

  if (currentStage < 1 || currentStage > 3) currentStage = 1;
  if (visualCredits > TOTAL_VISUAL_CREDITS) visualCredits = TOTAL_VISUAL_CREDITS;
  if (hungerRequestsShown > 15) hungerRequestsShown = 15;
  if (petRequestsShown > 15) petRequestsShown = 15;
  if (savedActiveMs > TOTAL_DEMO_MS) savedActiveMs = TOTAL_DEMO_MS;

  if (savedActiveMs >= TOTAL_DEMO_MS) {
    completionFlag = true;
  }

  if (completionFlag) {
    savedActiveMs = TOTAL_DEMO_MS;
    currentStage = 3;
    visualCredits = TOTAL_VISUAL_CREDITS;
    stageCareMask = 0x07;
  }

  // On reboot, the active clock waits for a new interaction before resuming.
  demoClockRunning = false;
  activeRunStartMs = 0;
}

void resetDemoState(uint32_t now) {
  prefs.clear();
  prefs.putUInt("ver", NVS_STATE_VERSION);

  demoStarted = false;
  demoClockRunning = false;
  completionFlag = false;

  savedActiveMs = 0;
  activeRunStartMs = 0;
  lastInteractionMs = now;
  lastNvsCheckpointElapsed = 0;

  currentStage = 1;
  stageCareMask = 0;
  visualCredits = 0;

  hungerRequestStage = 1;
  hungerRequestsShown = 0;
  nextHungerRequestAt = 0;
  hungerPromptActive = false;
  hungerPromptTracked = false;
  hungerPromptManualPreview = false;
  hungerPromptStart = 0;
  hungerRetryPending = false;

  petRequestStage = 1;
  petRequestsShown = 0;
  nextPetRequestAt = 0;
  petRequestPromptActive = false;
  petRequestPromptTracked = false;
  petRequestPromptManualPreview = false;
  petRequestPromptStart = 0;
  petRequestRetryPending = false;

  pendingUnlockMask = 0;
  pendingCompletion = false;

  savePersistentState(now);

  Serial.println();
  Serial.println("*** DEMO STATE RESET ***");
  Serial.println("Next user interaction starts Stage 1 timer.");
}

void notifyUserInteraction(uint32_t now) {
  lastInteractionMs = now;

  if (completionFlag) {
    return;
  }

  if (!demoStarted) {
    demoStarted = true;
    savedActiveMs = 0;
    lastNvsCheckpointElapsed = 0;
    Serial.println("DEMO CLOCK STARTED");
  }

  if (!demoClockRunning) {
    demoClockRunning = true;
    activeRunStartMs = now;
    Serial.println("DEMO CLOCK RESUMED");
  }
}

// ============================================================
// PROGRESS RING PULSE
// ============================================================

uint32_t ringPulseStart = 0;
const uint32_t RING_PULSE_MS = 850;

void triggerRingPulse(uint32_t now) {
  ringPulseStart = now;
}

// ============================================================
// REACTION LED - PWM + NON-BLOCKING PULSE
// Supports Arduino-ESP32 2.x and 3.x APIs.
// ============================================================

const uint32_t LED_PWM_FREQ = 5000;
const uint8_t LED_PWM_RES_BITS = 8;
const uint8_t LED_PWM_OLD_CHANNEL = 0;

const uint8_t LED_BASE_DUTY = 31;          // ~12%
const uint8_t LED_REACTION_PEAK = 180;     // ~71%
const uint8_t LED_STAGE_PEAK = 230;        // ~90%
const uint8_t LED_COMPLETE_PEAK = 255;     // 100%

bool ledPulseActive = false;
uint32_t ledPulseStart = 0;
uint8_t ledPulsePeak = LED_REACTION_PEAK;
uint8_t ledPulseRepeats = 1;

const uint32_t LED_ATTACK_MS = 180;
const uint32_t LED_HOLD_MS = 220;
const uint32_t LED_RELEASE_MS = 450;
const uint32_t LED_GAP_MS = 150;
const uint32_t LED_CYCLE_MS = LED_ATTACK_MS + LED_HOLD_MS + LED_RELEASE_MS + LED_GAP_MS;

void setReactionLedDuty(uint8_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(REACTION_LED_PIN, duty);
#else
  ledcWrite(LED_PWM_OLD_CHANNEL, duty);
#endif
}

void setupReactionLed() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  bool ok = ledcAttach(REACTION_LED_PIN, LED_PWM_FREQ, LED_PWM_RES_BITS);
  if (!ok) {
    Serial.println("WARNING: LED PWM attach failed");
  }
#else
  ledcSetup(LED_PWM_OLD_CHANNEL, LED_PWM_FREQ, LED_PWM_RES_BITS);
  ledcAttachPin(REACTION_LED_PIN, LED_PWM_OLD_CHANNEL);
#endif

  setReactionLedDuty(LED_BASE_DUTY);
}

void triggerLedPulse(uint8_t peak, uint8_t repeats, uint32_t now) {
  if (peak < LED_BASE_DUTY) peak = LED_BASE_DUTY;
  if (repeats < 1) repeats = 1;

  ledPulsePeak = peak;
  ledPulseRepeats = repeats;
  ledPulseStart = now;
  ledPulseActive = true;
}

void updateReactionLed(uint32_t now) {
  uint8_t duty = LED_BASE_DUTY;

  if (ledPulseActive) {
    uint32_t elapsed = now - ledPulseStart;
    uint32_t total = (uint32_t)ledPulseRepeats * LED_CYCLE_MS;

    if (elapsed >= total) {
      ledPulseActive = false;
      duty = LED_BASE_DUTY;
    } else {
      uint32_t phase = elapsed % LED_CYCLE_MS;
      float level = 0.0f;

      if (phase < LED_ATTACK_MS) {
        level = smoothStep((float)phase / (float)LED_ATTACK_MS);
      } else if (phase < LED_ATTACK_MS + LED_HOLD_MS) {
        level = 1.0f;
      } else if (phase < LED_ATTACK_MS + LED_HOLD_MS + LED_RELEASE_MS) {
        uint32_t releasePhase = phase - LED_ATTACK_MS - LED_HOLD_MS;
        level = 1.0f - smoothStep((float)releasePhase / (float)LED_RELEASE_MS);
      } else {
        level = 0.0f;
      }

      float out = (float)LED_BASE_DUTY + ((float)ledPulsePeak - (float)LED_BASE_DUTY) * level;
      if (out < 0.0f) out = 0.0f;
      if (out > 255.0f) out = 255.0f;
      duty = (uint8_t)out;
    }
  }

  setReactionLedDuty(duty);
}

// ============================================================
// EYE DYNAMICS
// ============================================================

struct EyeMotion {
  float x;
  float y;
  float targetX;
  float targetY;
};

EyeMotion leftEye  = {0, 0, 0, 0};
EyeMotion rightEye = {0, 0, 0, 0};

float idleX = 0.0f;
float idleY = 0.0f;
uint32_t nextIdleLook = 0;

float happyBlend = 0.0f;
float happyTarget = 0.0f;

float surpriseBlend = 0.0f;
float surpriseTarget = 0.0f;

float sleepClose = 0.0f;
float sleepCloseTarget = 0.0f;

// Sleep is a persistent RFID-driven state.
// TAG PRESENT = stay asleep. TAG REMOVED = smooth wake-up.
bool sleepTagPresent = false;
uint32_t sleepWakeStart = 0;
const uint32_t SLEEP_WAKE_MS = 1700;

float specialGlow = 0.0f;
float specialGlowTarget = 0.0f;

// ============================================================
// BLINK ENGINE
// ============================================================

enum BlinkState : uint8_t {
  BLINK_OPEN,
  BLINK_CLOSING,
  BLINK_CLOSED,
  BLINK_OPENING
};

BlinkState blinkState = BLINK_OPEN;
float blinkAmount = 0.0f;
uint32_t blinkStart = 0;
uint32_t nextBlink = 0;

const uint32_t BLINK_CLOSE_MS = 90;
const uint32_t BLINK_HOLD_MS = 45;
const uint32_t BLINK_OPEN_MS = 135;

void scheduleNextBlink(uint32_t now) {
  nextBlink = now + random(2600, 5600);
}

void startBlink(uint32_t now) {
  if (blinkState != BLINK_OPEN) return;
  blinkState = BLINK_CLOSING;
  blinkStart = now;
}

// ============================================================
// REACTION STATE
// ============================================================

enum Reaction : uint8_t {
  R_NONE,
  R_PET,
  R_FOOD,
  R_SLEEP,
  R_CONFUSED,
  R_UNLOCK2,
  R_UNLOCK3,
  R_COMPLETE
};

Reaction reaction = R_NONE;
uint32_t reactionStart = 0;
bool foodBlinkTriggered = false;

// A user interaction is never allowed to disappear just because another
// short animation is playing. We coalesce pending visuals by class instead
// of building an unbounded queue of stale animations.
bool pendingPetVisual = false;
uint8_t pendingFoodVisual = 0;   // 0 none, 1/2 food tag number
bool pendingConfusedVisual = false;
bool pendingSleepVisual = false;

// Brief neutral handoff between user-facing reactions. This lets the smoothed
// happy/surprise/glow values return toward neutral instead of leaking a strong
// expression from the previous reaction into the next one.
const uint32_t REACTION_SETTLE_MS = 240;
uint32_t userReactionReadyAt = 0;

bool userReactionCanStart(uint32_t now) {
  return (int32_t)(now - userReactionReadyAt) >= 0;
}

void beginReactionSettle(uint32_t now) {
  userReactionReadyAt = now + REACTION_SETTLE_MS;
  blinkState = BLINK_OPEN;
  blinkAmount = 0.0f;
  scheduleNextBlink(now);
}

bool reactionIsBusy() {
  return reaction != R_NONE;
}

bool reactionIsSystemPriority() {
  return reaction == R_UNLOCK2 ||
         reaction == R_UNLOCK3 ||
         reaction == R_COMPLETE;
}

void clearPendingUserVisuals() {
  pendingPetVisual = false;
  pendingFoodVisual = 0;
  pendingConfusedVisual = false;
  pendingSleepVisual = false;
}

// ============================================================
// IDLE LOOKING
// ============================================================

void chooseIdleLook(uint32_t now) {
  // More playful idle: larger gaze range and more frequent side-to-side looks.
  // Horizontal and diagonal choices are intentionally more common than center.
  int choice = random(12);

  switch (choice) {
    case 0:  idleX = -20; idleY =  0; break;
    case 1:  idleX = +20; idleY =  0; break;
    case 2:  idleX = -18; idleY = -7; break;
    case 3:  idleX = +18; idleY = -7; break;
    case 4:  idleX = -16; idleY = +7; break;
    case 5:  idleX = +16; idleY = +7; break;
    case 6:  idleX = -12; idleY = -10; break;
    case 7:  idleX = +12; idleY = -10; break;
    case 8:  idleX = -21; idleY = -2; break;
    case 9:  idleX = +21; idleY = -2; break;
    case 10: idleX =   0; idleY = -9; break;
    default: idleX =   0; idleY =  0; break;
  }

  long minDelay = 850;
  long maxDelay = 1900;

  if (currentStage == 2) {
    minDelay = 850;
    maxDelay = 1750;
  } else if (currentStage == 3) {
    minDelay = 750;
    maxDelay = 1600;
  }

  nextIdleLook = now + random(minDelay, maxDelay);
}

// ============================================================
// AUTONOMOUS PERSONALITY
//
// These are spontaneous eye-only expressions. They never create progress,
// never pulse the interaction LED, and never count as user activity.
//
// The generic personality scheduler uses constrained randomness:
//   - irregular SHORT / MEDIUM / LONG delay classes
//   - weighted state selection with per-cycle jitter
//   - recent-history suppression rather than a fixed sequence
//   - contextual Play weighting after quiet periods
//
// Hunger is NOT an AutonomousState. A dedicated Stage-aware overlay scheduler
// below owns up to 15 completed 10-second Hunger prompts per Stage while the
// normal eyes continue their existing idle/autonomous/reaction behavior.
//
// User/system events always discard an active autonomous expression.
// ============================================================

// Arduino's .ino preprocessor auto-generates function prototypes before some
// later type declarations. Keep AutonomousState for internal state storage, but
// use uint8_t at function-signature boundaries so generated prototypes never
// depend on this enum being declared first.
enum AutonomousState : uint8_t {
  AUTO_NONE,
  AUTO_LOOK,
  AUTO_WINK,
  AUTO_SMILE,
  AUTO_PLAY
};

AutonomousState autonomousState = AUTO_NONE;
uint32_t autonomousStart = 0;
uint32_t autonomousDuration = 0;
uint32_t nextAutonomousAt = 0;
uint8_t autonomousVariant = 0;
bool autonomousWinkLeft = true;

AutonomousState recentAutonomous[3] = {AUTO_NONE, AUTO_NONE, AUTO_NONE};

const uint32_t AUTO_SHORT_MIN_MS = 8UL * 1000UL;
const uint32_t AUTO_SHORT_MAX_MS = 20UL * 1000UL;
const uint32_t AUTO_MEDIUM_MIN_MS = 20UL * 1000UL;
const uint32_t AUTO_MEDIUM_MAX_MS = 55UL * 1000UL;
const uint32_t AUTO_LONG_MIN_MS = 55UL * 1000UL;
const uint32_t AUTO_LONG_MAX_MS = 120UL * 1000UL;

// Coordinated Stage-aware Care Request contract.
// Hunger and PET Request each allow 15 completed 10-second prompts per Stage.
// They never overlap. The shared gap envelope is intentionally compact so
// both unsatisfied needs can still present all 30 prompts within ~10 minutes.
const uint8_t HUNGER_REQUESTS_PER_STAGE = 15;
const uint8_t PET_REQUESTS_PER_STAGE = 15;
const uint32_t HUNGER_REQUEST_DURATION_MS = 10UL * 1000UL;
const uint32_t PET_REQUEST_DURATION_MS = 10UL * 1000UL;

const uint32_t CARE_REQUEST_FIRST_GAP_MIN_MS = 8UL * 1000UL;
const uint32_t CARE_REQUEST_FIRST_GAP_MAX_MS = 20UL * 1000UL;
const uint32_t CARE_REQUEST_NEXT_GAP_MIN_MS = 8UL * 1000UL;
const uint32_t CARE_REQUEST_NEXT_GAP_MAX_MS = 18UL * 1000UL;
const uint32_t CARE_REQUEST_RETRY_GAP_MIN_MS = 5UL * 1000UL;
const uint32_t CARE_REQUEST_RETRY_GAP_MAX_MS = 10UL * 1000UL;
const uint32_t CARE_REQUEST_COLLISION_DEFER_MIN_MS = 3UL * 1000UL;
const uint32_t CARE_REQUEST_COLLISION_DEFER_MAX_MS = 8UL * 1000UL;

const char *autonomousStateName(uint8_t state) {
  switch (state) {
    case AUTO_LOOK:   return "LOOK";
    case AUTO_WINK:   return "WINK";
    case AUTO_SMILE:  return "SMILE";
    case AUTO_PLAY:   return "PLAY_INVITE";
    default:          return "NONE";
  }
}

uint32_t chooseAutonomousDelayMs() {
  // Base intent is about 40% short, 40% medium, 20% long. Small threshold
  // jitter keeps the timing-class probabilities from feeling mechanically fixed.
  int shortWeight = 40 + (int)random(0, 13) - 6;
  int mediumWeight = 40 + (int)random(0, 13) - 6;
  int longWeight = 20 + (int)random(0, 9) - 4;

  if (shortWeight < 1) shortWeight = 1;
  if (mediumWeight < 1) mediumWeight = 1;
  if (longWeight < 1) longWeight = 1;

  int total = shortWeight + mediumWeight + longWeight;
  int roll = (int)random(total);

  if (roll < shortWeight) {
    return (uint32_t)random((long)AUTO_SHORT_MIN_MS, (long)AUTO_SHORT_MAX_MS + 1L);
  }

  roll -= shortWeight;
  if (roll < mediumWeight) {
    return (uint32_t)random((long)AUTO_MEDIUM_MIN_MS, (long)AUTO_MEDIUM_MAX_MS + 1L);
  }

  return (uint32_t)random((long)AUTO_LONG_MIN_MS, (long)AUTO_LONG_MAX_MS + 1L);
}

void scheduleNextAutonomous(uint32_t now) {
  nextAutonomousAt = now + chooseAutonomousDelayMs();
}

void rememberAutonomous(uint8_t state) {
  recentAutonomous[2] = recentAutonomous[1];
  recentAutonomous[1] = recentAutonomous[0];
  recentAutonomous[0] = (AutonomousState)state;
}

int applyAutonomousHistoryPenalty(uint8_t state, int weight) {
  if (recentAutonomous[0] == state) {
    weight = (weight * 25) / 100;
  } else if (recentAutonomous[1] == state || recentAutonomous[2] == state) {
    weight = (weight * 60) / 100;
  }

  if (weight < 1) weight = 1;
  return weight;
}

uint8_t chooseAutonomousState(uint32_t now) {
  int weights[4] = {
    30, // LOOK
    20, // WINK
    20, // SMILE
    18  // PLAY
  };

  // Personality gets richer by stage without removing any generic behavior family.
  if (currentStage == 2) {
    weights[1] += 2;
    weights[2] += 3;
    weights[3] += 3;
  } else if (currentStage == 3) {
    weights[1] += 3;
    weights[2] += 5;
    weights[3] += 5;
  }

  // A longer quiet period makes a friendly play invitation more likely, but
  // never schedules it at a deterministic timeout.
  uint32_t quietMs = now - lastInteractionMs;
  if (quietMs >= 30UL * 1000UL) weights[3] += 4;
  if (quietMs >= 60UL * 1000UL) weights[3] += 5;
  if (quietMs >= 120UL * 1000UL) weights[3] += 6;

  // Small independent per-cycle jitter changes effective priority.
  for (int i = 0; i < 4; i++) {
    weights[i] += (int)random(0, 11) - 5;
    if (weights[i] < 1) weights[i] = 1;
  }

  const uint8_t states[4] = {
    AUTO_LOOK, AUTO_WINK, AUTO_SMILE, AUTO_PLAY
  };

  for (int i = 0; i < 4; i++) {
    weights[i] = applyAutonomousHistoryPenalty(states[i], weights[i]);
  }

  int total = 0;
  for (int i = 0; i < 4; i++) total += weights[i];

  if (total <= 0) return AUTO_LOOK;

  int roll = (int)random(total);
  for (int i = 0; i < 4; i++) {
    if (roll < weights[i]) return states[i];
    roll -= weights[i];
  }

  return AUTO_LOOK;
}

uint32_t chooseAutonomousDurationMs(uint8_t state) {
  switch (state) {
    case AUTO_LOOK:
      return (uint32_t)random(1800L, 3601L);
    case AUTO_WINK:
      return (uint32_t)random(600L, 1201L);
    case AUTO_SMILE:
      return (uint32_t)random(1500L, 3501L);
    case AUTO_PLAY:
      return (uint32_t)random(2000L, 4001L);
    default:
      return 1000UL;
  }
}

void cancelAutonomous() {
  autonomousState = AUTO_NONE;
  autonomousStart = 0;
  autonomousDuration = 0;
}

void interruptAutonomousForInteraction(uint32_t now) {
  if (autonomousState != AUTO_NONE) {
    cancelAutonomous();
    blinkState = BLINK_OPEN;
    blinkAmount = 0.0f;
    scheduleNextBlink(now);
  }
}

void startAutonomous(uint8_t state, uint32_t now) {
  if (reaction != R_NONE) return;
  if (!userReactionCanStart(now)) return;

  autonomousState = (AutonomousState)state;
  autonomousStart = now;
  autonomousDuration = chooseAutonomousDurationMs(state);
  autonomousVariant = (uint8_t)random(4);
  autonomousWinkLeft = random(2) == 0;

  // Personality events own the eyelid/gaze presentation while active.
  blinkState = BLINK_OPEN;
  blinkAmount = 0.0f;

  rememberAutonomous(state);

  Serial.print("AUTONOMOUS -> ");
  Serial.print(autonomousStateName(state));
  Serial.print(" variant=");
  Serial.print(autonomousVariant);
  Serial.print(" duration=");
  Serial.print(autonomousDuration);
  Serial.println(" ms");
}


bool hungerNeedSatisfiedThisStage() {
  return completionFlag || ((stageCareMask & CARE_FOOD_BIT) != 0);
}

bool hungerOverlayBlockedByPriority() {
  return reaction == R_SLEEP ||
         reaction == R_UNLOCK2 ||
         reaction == R_UNLOCK3 ||
         reaction == R_COMPLETE ||
         pendingSleepVisual ||
         pendingCompletion ||
         pendingUnlockMask != 0;
}

void clearHungerPromptRuntime() {
  hungerPromptActive = false;
  hungerPromptTracked = false;
  hungerPromptManualPreview = false;
  hungerPromptStart = 0;
}

void scheduleNextHungerRequest(uint32_t now, bool retrySoon) {
  if (completionFlag ||
      hungerNeedSatisfiedThisStage() ||
      hungerRequestsShown >= HUNGER_REQUESTS_PER_STAGE) {
    nextHungerRequestAt = 0;
    return;
  }

  uint32_t gapMs = 0;

  if (retrySoon) {
    gapMs = (uint32_t)random(
      (long)CARE_REQUEST_RETRY_GAP_MIN_MS,
      (long)CARE_REQUEST_RETRY_GAP_MAX_MS + 1L
    );
  } else if (hungerRequestsShown == 0) {
    gapMs = (uint32_t)random(
      (long)CARE_REQUEST_FIRST_GAP_MIN_MS,
      (long)CARE_REQUEST_FIRST_GAP_MAX_MS + 1L
    );
  } else {
    gapMs = (uint32_t)random(
      (long)CARE_REQUEST_NEXT_GAP_MIN_MS,
      (long)CARE_REQUEST_NEXT_GAP_MAX_MS + 1L
    );
  }

  nextHungerRequestAt = now + gapMs;

  Serial.print("HUNGER OVERLAY SCHEDULED: stage=");
  Serial.print(currentStage);
  Serial.print(" completed=");
  Serial.print(hungerRequestsShown);
  Serial.print("/");
  Serial.print(HUNGER_REQUESTS_PER_STAGE);
  Serial.print(" in ");
  Serial.print(gapMs / 1000UL);
  Serial.println(" s");
}

void startHungerPrompt(uint32_t now, bool tracked) {
  hungerPromptActive = true;
  hungerPromptTracked = tracked;
  hungerPromptManualPreview = !tracked;
  hungerPromptStart = now;
  hungerRetryPending = false;

  Serial.print(tracked ? "*** HUNGER OVERLAY START " : "*** HUNGER OVERLAY PREVIEW ");
  if (tracked) {
    Serial.print(hungerRequestsShown + 1);
    Serial.print("/");
    Serial.print(HUNGER_REQUESTS_PER_STAGE);
    Serial.print(" ");
  }
  Serial.println("- 10 s, eyes remain active ***");
}

void interruptTrackedHungerPromptForRetry(uint32_t now) {
  if (!hungerPromptActive) return;

  bool wasTracked = hungerPromptTracked;
  clearHungerPromptRuntime();

  if (wasTracked &&
      !completionFlag &&
      !hungerNeedSatisfiedThisStage() &&
      hungerRequestsShown < HUNGER_REQUESTS_PER_STAGE) {
    hungerRetryPending = true;
    scheduleNextHungerRequest(now, true);
  } else {
    hungerRetryPending = false;
  }
}

void resetHungerRequestForStage(uint32_t now) {
  clearHungerPromptRuntime();

  hungerRequestStage = currentStage;
  hungerRequestsShown = 0;
  nextHungerRequestAt = 0;
  hungerRetryPending = false;

  if (!completionFlag && !hungerNeedSatisfiedThisStage()) {
    scheduleNextHungerRequest(now, false);
  }
}

void syncHungerRequestStage(uint32_t now) {
  if (hungerRequestStage != currentStage) {
    hungerRequestStage = currentStage;
    hungerRequestsShown = 0;
  }

  if (hungerRequestsShown > HUNGER_REQUESTS_PER_STAGE) {
    hungerRequestsShown = HUNGER_REQUESTS_PER_STAGE;
  }

  clearHungerPromptRuntime();
  nextHungerRequestAt = 0;
  hungerRetryPending = false;

  if (!completionFlag && !hungerNeedSatisfiedThisStage()) {
    scheduleNextHungerRequest(now, false);
  }
}

void satisfyHungerNeedForStage(uint32_t now) {
  clearHungerPromptRuntime();
  hungerRetryPending = false;
  nextHungerRequestAt = 0;

  Serial.print("HUNGER NEED SATISFIED - STAGE ");
  Serial.println(currentStage);
}

void completeTrackedHungerRequest(uint32_t now) {
  if (!hungerPromptActive || !hungerPromptTracked) return;

  clearHungerPromptRuntime();
  hungerRetryPending = false;

  if (hungerRequestsShown < HUNGER_REQUESTS_PER_STAGE) {
    hungerRequestsShown++;
  }

  Serial.print("HUNGER OVERLAY COMPLETE: stage=");
  Serial.print(currentStage);
  Serial.print(" completed=");
  Serial.print(hungerRequestsShown);
  Serial.print("/");
  Serial.println(HUNGER_REQUESTS_PER_STAGE);

  savePersistentState(now);

  if (!hungerNeedSatisfiedThisStage() &&
      hungerRequestsShown < HUNGER_REQUESTS_PER_STAGE) {
    scheduleNextHungerRequest(now, false);
  } else {
    nextHungerRequestAt = 0;
  }
}

void updateHungerRequestScheduler(uint32_t now) {
  if (hungerRequestStage != currentStage) {
    resetHungerRequestForStage(now);
  }

  if (completionFlag || hungerNeedSatisfiedThisStage()) {
    if (hungerPromptActive) clearHungerPromptRuntime();
    nextHungerRequestAt = 0;
    hungerRetryPending = false;
    return;
  }

  // SLEEP and System-priority visuals own the whole face. A tracked Hunger
  // overlay interrupted by them is retried and does not consume the 15 quota.
  if (hungerPromptActive && hungerOverlayBlockedByPriority()) {
    interruptTrackedHungerPromptForRetry(now);
    return;
  }

  if (hungerPromptActive) {
    if ((now - hungerPromptStart) >= HUNGER_REQUEST_DURATION_MS) {
      if (hungerPromptTracked) {
        completeTrackedHungerRequest(now);
      } else {
        clearHungerPromptRuntime();
      }
    }
    return;
  }

  if (hungerRequestsShown >= HUNGER_REQUESTS_PER_STAGE) {
    nextHungerRequestAt = 0;
    return;
  }

  if (nextHungerRequestAt == 0) {
    bool retrySoon = hungerRetryPending;
    hungerRetryPending = false;
    scheduleNextHungerRequest(now, retrySoon);
    return;
  }

  if ((int32_t)(now - nextHungerRequestAt) >= 0) {
    // Hunger is an overlay and can coexist with normal idle movement, Blink,
    // Wink/Smile/Play, PET and friendly unknown-card reactions. It only waits
    // for persistent Sleep or System-priority full-face reactions.
    if (hungerOverlayBlockedByPriority()) return;

    // Never show Hunger and PET Request simultaneously. If both are due on
    // the same loop, choose which need gets the visual slot randomly.
    bool petDueNow =
      nextPetRequestAt != 0 &&
      (int32_t)(now - nextPetRequestAt) >= 0 &&
      !completionFlag &&
      ((stageCareMask & CARE_PET_BIT) == 0) &&
      petRequestsShown < PET_REQUESTS_PER_STAGE;

    if (petRequestPromptActive) return;

    if (petDueNow && random(2) == 0) {
      uint32_t deferMs = (uint32_t)random(
        (long)CARE_REQUEST_COLLISION_DEFER_MIN_MS,
        (long)CARE_REQUEST_COLLISION_DEFER_MAX_MS + 1L
      );
      nextHungerRequestAt = now + deferMs;
      Serial.println("CARE REQUEST COLLISION -> PET REQUEST GETS THIS SLOT");
      return;
    }

    nextHungerRequestAt = 0;
    startHungerPrompt(now, true);
  }
}

// ============================================================
// PET REQUEST / AFFECTION REQUEST
//
// Mirrors Hunger lifecycle:
//   - up to 15 completed prompts per Stage
//   - 10 seconds each
//   - wall-clock/idle scheduler
//   - first valid PET in the Stage satisfies the need and cancels the rest
// Visual:
//   - normal eyes remain alive
//   - gaze is gently biased upward/inward while reaction==R_NONE
//   - a small animated hand/stroking cue appears low on both displays
// PET detector qualification itself is untouched.
// ============================================================

bool petRequestNeedSatisfiedThisStage() {
  return completionFlag || ((stageCareMask & CARE_PET_BIT) != 0);
}

bool petRequestBlockedByPriority() {
  // Unlike Hunger, a PET request owns a clear "please pet me" message. Any
  // real reaction temporarily dismisses it so FOOD/Unknown/Sleep feedback is
  // never visually mixed with the pet-request hand cue.
  return reaction != R_NONE ||
         pendingSleepVisual ||
         pendingCompletion ||
         pendingUnlockMask != 0;
}

void clearPetRequestPromptRuntime() {
  petRequestPromptActive = false;
  petRequestPromptTracked = false;
  petRequestPromptManualPreview = false;
  petRequestPromptStart = 0;
}

void scheduleNextPetRequest(uint32_t now, bool retrySoon) {
  if (completionFlag ||
      petRequestNeedSatisfiedThisStage() ||
      petRequestsShown >= PET_REQUESTS_PER_STAGE) {
    nextPetRequestAt = 0;
    return;
  }

  uint32_t gapMs = 0;

  if (retrySoon) {
    gapMs = (uint32_t)random(
      (long)CARE_REQUEST_RETRY_GAP_MIN_MS,
      (long)CARE_REQUEST_RETRY_GAP_MAX_MS + 1L
    );
  } else if (petRequestsShown == 0) {
    gapMs = (uint32_t)random(
      (long)CARE_REQUEST_FIRST_GAP_MIN_MS,
      (long)CARE_REQUEST_FIRST_GAP_MAX_MS + 1L
    );
  } else {
    gapMs = (uint32_t)random(
      (long)CARE_REQUEST_NEXT_GAP_MIN_MS,
      (long)CARE_REQUEST_NEXT_GAP_MAX_MS + 1L
    );
  }

  nextPetRequestAt = now + gapMs;

  Serial.print("PET REQUEST SCHEDULED: stage=");
  Serial.print(currentStage);
  Serial.print(" completed=");
  Serial.print(petRequestsShown);
  Serial.print("/");
  Serial.print(PET_REQUESTS_PER_STAGE);
  Serial.print(" in ");
  Serial.print(gapMs / 1000UL);
  Serial.println(" s");
}

void startPetRequestPrompt(uint32_t now, bool tracked) {
  // Care prompts must remain visually singular.
  if (hungerPromptActive) return;

  petRequestPromptActive = true;
  petRequestPromptTracked = tracked;
  petRequestPromptManualPreview = !tracked;
  petRequestPromptStart = now;
  petRequestRetryPending = false;

  Serial.print(tracked ? "*** PET REQUEST START " : "*** PET REQUEST PREVIEW ");
  if (tracked) {
    Serial.print(petRequestsShown + 1);
    Serial.print("/");
    Serial.print(PET_REQUESTS_PER_STAGE);
    Serial.print(" ");
  }
  Serial.println("- 10 s, affectionate eyes + hand cue ***");
}

void interruptTrackedPetRequestForRetry(uint32_t now) {
  if (!petRequestPromptActive) return;

  bool wasTracked = petRequestPromptTracked;
  clearPetRequestPromptRuntime();

  if (wasTracked &&
      !completionFlag &&
      !petRequestNeedSatisfiedThisStage() &&
      petRequestsShown < PET_REQUESTS_PER_STAGE) {
    petRequestRetryPending = true;
    scheduleNextPetRequest(now, true);
  } else {
    petRequestRetryPending = false;
  }
}

void resetPetRequestForStage(uint32_t now) {
  clearPetRequestPromptRuntime();

  petRequestStage = currentStage;
  petRequestsShown = 0;
  nextPetRequestAt = 0;
  petRequestRetryPending = false;

  if (!completionFlag && !petRequestNeedSatisfiedThisStage()) {
    scheduleNextPetRequest(now, false);
  }
}

void syncPetRequestStage(uint32_t now) {
  if (petRequestStage != currentStage) {
    petRequestStage = currentStage;
    petRequestsShown = 0;
  }

  if (petRequestsShown > PET_REQUESTS_PER_STAGE) {
    petRequestsShown = PET_REQUESTS_PER_STAGE;
  }

  clearPetRequestPromptRuntime();
  nextPetRequestAt = 0;
  petRequestRetryPending = false;

  if (!completionFlag && !petRequestNeedSatisfiedThisStage()) {
    scheduleNextPetRequest(now, false);
  }
}

void satisfyPetRequestNeedForStage(uint32_t now) {
  clearPetRequestPromptRuntime();
  petRequestRetryPending = false;
  nextPetRequestAt = 0;

  Serial.print("PET REQUEST NEED SATISFIED - STAGE ");
  Serial.println(currentStage);
}

void completeTrackedPetRequest(uint32_t now) {
  if (!petRequestPromptActive || !petRequestPromptTracked) return;

  clearPetRequestPromptRuntime();
  petRequestRetryPending = false;

  if (petRequestsShown < PET_REQUESTS_PER_STAGE) {
    petRequestsShown++;
  }

  Serial.print("PET REQUEST COMPLETE: stage=");
  Serial.print(currentStage);
  Serial.print(" completed=");
  Serial.print(petRequestsShown);
  Serial.print("/");
  Serial.println(PET_REQUESTS_PER_STAGE);

  savePersistentState(now);

  if (!petRequestNeedSatisfiedThisStage() &&
      petRequestsShown < PET_REQUESTS_PER_STAGE) {
    scheduleNextPetRequest(now, false);
  } else {
    nextPetRequestAt = 0;
  }
}

void updatePetRequestScheduler(uint32_t now) {
  if (petRequestStage != currentStage) {
    resetPetRequestForStage(now);
  }

  if (completionFlag || petRequestNeedSatisfiedThisStage()) {
    if (petRequestPromptActive) clearPetRequestPromptRuntime();
    nextPetRequestAt = 0;
    petRequestRetryPending = false;
    return;
  }

  // Never overlap the two care-request visuals.
  if (hungerPromptActive) {
    return;
  }

  if (petRequestPromptActive && petRequestBlockedByPriority()) {
    interruptTrackedPetRequestForRetry(now);
    return;
  }

  if (petRequestPromptActive) {
    if ((now - petRequestPromptStart) >= PET_REQUEST_DURATION_MS) {
      if (petRequestPromptTracked) {
        completeTrackedPetRequest(now);
      } else {
        clearPetRequestPromptRuntime();
      }
    }
    return;
  }

  if (petRequestsShown >= PET_REQUESTS_PER_STAGE) {
    nextPetRequestAt = 0;
    return;
  }

  if (nextPetRequestAt == 0) {
    bool retrySoon = petRequestRetryPending;
    petRequestRetryPending = false;
    scheduleNextPetRequest(now, retrySoon);
    return;
  }

  if ((int32_t)(now - nextPetRequestAt) >= 0) {
    if (petRequestBlockedByPriority() || hungerPromptActive) return;

    nextPetRequestAt = 0;
    startPetRequestPrompt(now, true);
  }
}

void updateAutonomous(uint32_t now) {
  if (reaction != R_NONE) {
    if (autonomousState != AUTO_NONE) cancelAutonomous();
    return;
  }

  if (autonomousState != AUTO_NONE) {
    if ((now - autonomousStart) >= autonomousDuration) {
      cancelAutonomous();
      chooseIdleLook(now);
      scheduleNextBlink(now);
      scheduleNextAutonomous(now);
    }
    return;
  }

  if (!userReactionCanStart(now)) return;

  if (nextAutonomousAt == 0) {
    scheduleNextAutonomous(now);
    return;
  }

  if ((int32_t)(now - nextAutonomousAt) >= 0) {
    startAutonomous(chooseAutonomousState(now), now);
  }
}

float autonomousLidAmount(bool leftSide, uint32_t now) {
  if (reaction != R_NONE || autonomousState == AUTO_NONE || autonomousDuration == 0) {
    return 0.0f;
  }

  float p = clamp01((float)(now - autonomousStart) / (float)autonomousDuration);

  if (autonomousState == AUTO_WINK) {
    bool targetEye = autonomousWinkLeft ? leftSide : !leftSide;
    if (!targetEye) return 0.0f;

    if (p < 0.28f) return smoothStep(p / 0.28f);
    if (p < 0.62f) return 1.0f;
    return 1.0f - smoothStep((p - 0.62f) / 0.38f);
  }

  // One Play variant contains a brief half-wink; the other variants use only
  // gaze/bounce changes. This adds variation without turning Play into Wink.
  if (autonomousState == AUTO_PLAY && autonomousVariant == 2) {
    bool targetEye = autonomousWinkLeft ? leftSide : !leftSide;
    if (!targetEye || p < 0.42f || p > 0.76f) return 0.0f;
    float local = (p - 0.42f) / 0.34f;
    return sinf(local * PI) * 0.60f;
  }

  return 0.0f;
}

float petRequestLidAmount(uint32_t now) {
  if (!petRequestPromptActive || reaction != R_NONE) return 0.0f;

  uint32_t elapsed = now - petRequestPromptStart;
  if (elapsed >= PET_REQUEST_DURATION_MS) return 0.0f;

  float p = (float)elapsed / (float)PET_REQUEST_DURATION_MS;
  // Two very shallow "please?" eyelid softens; never approaches a blink.
  float pulse = 0.5f + 0.5f * sinf(p * TWO_PI * 2.0f - PI * 0.5f);
  return pulse * 0.09f;
}

void applyAutonomousEyeTargets(uint32_t now) {
  if (autonomousState == AUTO_NONE || autonomousDuration == 0) return;

  uint32_t elapsed = now - autonomousStart;
  float p = clamp01((float)elapsed / (float)autonomousDuration);
  float stageScale = (currentStage == 1) ? 0.85f : (currentStage == 2 ? 1.0f : 1.15f);

  if (autonomousState == AUTO_LOOK) {
    specialGlowTarget = (currentStage == 1) ? 0.02f : (currentStage == 2 ? 0.09f : 0.17f);

    if (autonomousVariant == 0) {
      if (p < 0.40f) {
        leftEye.targetX = -18.0f; rightEye.targetX = -18.0f;
        leftEye.targetY = -3.0f;  rightEye.targetY = -3.0f;
      } else if (p < 0.78f) {
        leftEye.targetX = +15.0f; rightEye.targetX = +15.0f;
        leftEye.targetY = -8.0f;  rightEye.targetY = -8.0f;
      } else {
        leftEye.targetX = 0.0f; rightEye.targetX = 0.0f;
        leftEye.targetY = 0.0f; rightEye.targetY = 0.0f;
      }
    } else if (autonomousVariant == 1) {
      if (p < 0.58f) {
        leftEye.targetX = +20.0f; rightEye.targetX = +20.0f;
        leftEye.targetY = +2.0f;  rightEye.targetY = +2.0f;
      } else {
        leftEye.targetX = 0.0f; rightEye.targetX = 0.0f;
        leftEye.targetY = -5.0f; rightEye.targetY = -5.0f;
      }
    } else {
      float sweep = sinf(p * TWO_PI * 0.75f) * 18.0f;
      leftEye.targetX = sweep;
      rightEye.targetX = sweep;
      leftEye.targetY = -4.0f + sinf(p * TWO_PI) * 4.0f;
      rightEye.targetY = leftEye.targetY;
    }
    return;
  }

  if (autonomousState == AUTO_WINK) {
    happyTarget = 0.22f * stageScale;
    specialGlowTarget = 0.08f * stageScale;
    float side = autonomousWinkLeft ? -1.0f : 1.0f;
    leftEye.targetX = side * 4.0f;
    rightEye.targetX = side * 4.0f;
    leftEye.targetY = -2.0f;
    rightEye.targetY = -2.0f;
    return;
  }

  if (autonomousState == AUTO_SMILE) {
    float enter = smoothStep(clamp01((float)elapsed / 450.0f));
    float exit = 1.0f;
    if (autonomousDuration > 550UL && elapsed > autonomousDuration - 550UL) {
      exit = 1.0f - smoothStep(
        clamp01((float)(elapsed - (autonomousDuration - 550UL)) / 550.0f)
      );
    }
    float strength = enter * exit;

    happyTarget = 0.58f * stageScale * strength;
    surpriseTarget = 0.10f * strength;
    specialGlowTarget = 0.18f * stageScale * strength;
    leftEye.targetX = +4.0f * strength;
    rightEye.targetX = -4.0f * strength;
    leftEye.targetY = -5.0f * strength;
    rightEye.targetY = -5.0f * strength;
    return;
  }

  if (autonomousState == AUTO_PLAY) {
    float enter = smoothStep(clamp01((float)elapsed / 350.0f));
    float exit = 1.0f;
    if (autonomousDuration > 500UL && elapsed > autonomousDuration - 500UL) {
      exit = 1.0f - smoothStep(
        clamp01((float)(elapsed - (autonomousDuration - 500UL)) / 500.0f)
      );
    }
    float strength = enter * exit;

    happyTarget = 0.30f * stageScale * strength;
    surpriseTarget = 0.38f * stageScale * strength;
    specialGlowTarget = 0.16f * stageScale * strength;

    if (autonomousVariant == 0) {
      float sweep = sinf(p * TWO_PI * 1.15f) * 12.0f;
      leftEye.targetX = sweep;
      rightEye.targetX = sweep;
      leftEye.targetY = -3.0f - fabsf(sinf(p * TWO_PI * 1.15f)) * 3.0f * stageScale;
      rightEye.targetY = leftEye.targetY;
    } else if (autonomousVariant == 1) {
      float bounce = -fabsf(sinf(p * PI * 3.0f)) * 5.0f * stageScale;
      leftEye.targetX = +4.0f * strength;
      rightEye.targetX = -4.0f * strength;
      leftEye.targetY = bounce;
      rightEye.targetY = bounce;
    } else {
      float glance = (p < 0.36f) ? -11.0f : ((p < 0.68f) ? +11.0f : 0.0f);
      leftEye.targetX = glance;
      rightEye.targetX = glance;
      leftEye.targetY = -4.0f * strength;
      rightEye.targetY = -4.0f * strength;
    }
    return;
  }


}


void applyPetRequestEyeCue(uint32_t now) {
  if (!petRequestPromptActive || reaction != R_NONE) return;

  uint32_t elapsed = now - petRequestPromptStart;
  float enter = smoothStep(clamp01((float)elapsed / 650.0f));
  float exit = 1.0f;

  if (elapsed > PET_REQUEST_DURATION_MS - 700UL) {
    exit = 1.0f - smoothStep(
      clamp01((float)(elapsed - (PET_REQUEST_DURATION_MS - 700UL)) / 700.0f)
    );
  }

  float strength = enter * exit;
  float blend = 0.58f * strength;
  float tiny = sinf((float)elapsed * 0.0030f) * 0.8f;

  // Softer than the actual PET reaction: request pose is inviting, while the
  // real PET reward still jumps much further upward/inward and glows stronger.
  float requestLeftX = +7.0f + tiny;
  float requestRightX = -7.0f - tiny;
  float requestY = -13.0f + sinf((float)elapsed * 0.0022f) * 0.7f;

  leftEye.targetX = leftEye.targetX * (1.0f - blend) + requestLeftX * blend;
  rightEye.targetX = rightEye.targetX * (1.0f - blend) + requestRightX * blend;
  leftEye.targetY = leftEye.targetY * (1.0f - blend) + requestY * blend;
  rightEye.targetY = rightEye.targetY * (1.0f - blend) + requestY * blend;

  float requestHappy = 0.20f * strength;
  float requestGlow = 0.20f * strength;
  if (happyTarget < requestHappy) happyTarget = requestHappy;
  if (specialGlowTarget < requestGlow) specialGlowTarget = requestGlow;
}

// ============================================================
// PROGRESS CREDIT LOGIC
// ============================================================

void printProgressState() {
  float pct = ((float)visualCredits / (float)TOTAL_VISUAL_CREDITS) * 100.0f;

  Serial.print("STAGE: ");
  Serial.print(currentStage);
  Serial.print(" | PROGRESS: ");
  Serial.print(visualCredits);
  Serial.print("/9 = ");
  Serial.print(pct, 1);
  Serial.println("%");
}

bool registerCareCredit(uint8_t careBit, const char *label, uint32_t now) {
  if (completionFlag) {
    Serial.print(label);
    Serial.println(" -> REACTION ONLY (DEMO COMPLETE)");
    return false;
  }

  if ((stageCareMask & careBit) != 0) {
    Serial.print(label);
    Serial.println(" -> REACTION ONLY (ALREADY CREDITED IN THIS STAGE)");
    return false;
  }

  uint8_t stageCap = currentStage * 3;

  if (visualCredits >= stageCap) {
    Serial.print(label);
    Serial.println(" -> REACTION ONLY (STAGE PROGRESS FULL)");
    return false;
  }

  stageCareMask |= careBit;
  visualCredits++;

  if (visualCredits > stageCap) {
    visualCredits = stageCap;
  }

  triggerRingPulse(now);
  savePersistentState(now);

  Serial.print(label);
  Serial.println(" -> +1 PROGRESS");
  printProgressState();

  return true;
}

// ============================================================
// REACTION STARTERS
// Visual only; event handlers below manage progress + LED + clock.
// ============================================================

void startPetReaction(uint32_t now) {
  // A second valid PET is allowed to restart/refresh the PET animation.
  // Other atomic reactions still keep their priority and are not interrupted.
  if (reactionIsBusy() && reaction != R_PET) return;

  reaction = R_PET;
  reactionStart = now;

  // PET should read immediately as a deliberate affectionate look.
  // Cancel an incidental idle blink so the first part of the reaction
  // is not hidden by a blink that happened to start at the same moment.
  blinkState = BLINK_OPEN;
  blinkAmount = 0.0f;
  nextBlink = now + 5600UL + (uint32_t)random(0, 1800);

  Serial.println("PET REACTION - CLEAR AFFECTIONATE UP LOOK");
}

void startFoodReaction(uint32_t now) {
  if (reactionIsBusy()) return;
  reaction = R_FOOD;
  reactionStart = now;
  foodBlinkTriggered = false;
  Serial.println("FOOD REACTION");
}

void startSleepReaction(uint32_t now) {
  if (reactionIsBusy()) return;

  reaction = R_SLEEP;
  reactionStart = now;
  sleepTagPresent = true;
  sleepWakeStart = 0;

  Serial.println("SLEEP STATE ENTERED - WAITING FOR TAG REMOVAL");
}

void beginSleepWake(uint32_t now) {
  if (reaction != R_SLEEP) return;
  if (!sleepTagPresent && sleepWakeStart != 0) return;

  sleepTagPresent = false;
  sleepWakeStart = now;
  Serial.println("SLEEP TAG REMOVED -> WAKE UP");
}

void startConfusedReaction(uint32_t now) {
  if (reactionIsBusy()) return;
  reaction = R_CONFUSED;
  reactionStart = now;
  Serial.println("UNKNOWN RFID -> FRIENDLY CONFUSED REACTION");
}


void startUnlockReaction(uint8_t stageNumber, uint32_t now) {
  if (reactionIsBusy()) return;

  cancelAutonomous();

  if (stageNumber == 2) {
    reaction = R_UNLOCK2;
    reactionStart = now;
    triggerLedPulse(LED_STAGE_PEAK, 2, now);
    Serial.println("*** STAGE 2 UNLOCK ***");
  } else if (stageNumber == 3) {
    reaction = R_UNLOCK3;
    reactionStart = now;
    triggerLedPulse(LED_STAGE_PEAK, 2, now);
    Serial.println("*** STAGE 3 UNLOCK ***");
  }
}

void startCompletionReaction(uint32_t now) {
  if (reactionIsBusy()) return;

  cancelAutonomous();
  reaction = R_COMPLETE;
  reactionStart = now;
  triggerLedPulse(LED_COMPLETE_PEAK, 3, now);
  Serial.println();
  Serial.println("====================================");
  Serial.println("TANDO DEMO COMPLETE - 100%");
  Serial.println("Progress is now locked. Reactions remain active.");
  Serial.println("====================================");
}

// ============================================================
// USER EVENT HANDLERS
// ============================================================

void handlePetEvent(uint32_t now) {
  // PET is intentionally disabled while sleeping. This prevents capacitive
  // noise or a hand resting on the enclosure from earning hidden PET credit.
  if (reaction == R_SLEEP || pendingSleepVisual) {
    Serial.println("PET IGNORED - SLEEP ACTIVE");
    return;
  }

  satisfyPetRequestNeedForStage(now);
  interruptAutonomousForInteraction(now);
  notifyUserInteraction(now);
  triggerLedPulse(LED_REACTION_PEAK, 1, now);
  registerCareCredit(CARE_PET_BIT, "PET", now);

  if ((reaction == R_NONE && userReactionCanStart(now)) || reaction == R_PET) {
    // Repeated valid petting refreshes the affectionate pose immediately.
    startPetReaction(now);
  } else {
    pendingPetVisual = true;
  }
}

void handleFoodEvent(uint8_t foodNumber, uint32_t now) {
  // A physical SLEEP tag owns the interaction state until it is removed.
  if (reaction == R_SLEEP || pendingSleepVisual) {
    Serial.println("FOOD IGNORED - SLEEP ACTIVE");
    return;
  }

  satisfyHungerNeedForStage(now);
  interruptAutonomousForInteraction(now);
  notifyUserInteraction(now);
  triggerLedPulse(LED_REACTION_PEAK, 1, now);

  if (foodNumber == 1) {
    Serial.println("FOOD TAG 1 ACCEPTED");
  } else {
    Serial.println("FOOD TAG 2 ACCEPTED");
  }

  registerCareCredit(CARE_FOOD_BIT, "FOOD", now);

  if (!reactionIsBusy() && userReactionCanStart(now)) {
    startFoodReaction(now);
  } else {
    // Both food tags use the same visual, but keep the latest tag number for logs.
    pendingFoodVisual = foodNumber;
  }
}

void handleSleepEvent(uint32_t now) {
  interruptAutonomousForInteraction(now);
  notifyUserInteraction(now);
  triggerLedPulse(LED_REACTION_PEAK, 1, now);
  registerCareCredit(CARE_SLEEP_BIT, "SLEEP", now);

  // The physical SLEEP tag is persistent and has priority over ordinary
  // PET/FOOD/CONFUSED animations. System unlock/completion animations finish
  // first, then sleep starts if the tag is still present.
  sleepTagPresent = true;
  pendingPetVisual = false;
  pendingFoodVisual = 0;
  pendingConfusedVisual = false;

  if (reactionIsSystemPriority()) {
    pendingSleepVisual = true;
    return;
  }

  pendingSleepVisual = false;

  if (reaction != R_SLEEP) {
    if (reactionIsBusy()) {
      // SLEEP still preempts ordinary user reactions, but allow a short
      // neutral visual handoff so the previous expression does not leak in.
      reaction = R_NONE;
      foodBlinkTriggered = false;
      beginReactionSettle(now);
    }

    if (userReactionCanStart(now)) {
      startSleepReaction(now);
    } else {
      pendingSleepVisual = true;
    }
  }
}

void handleUnknownRfidEvent(uint32_t now) {
  if (reaction == R_SLEEP || pendingSleepVisual) return;

  interruptAutonomousForInteraction(now);
  notifyUserInteraction(now);
  triggerLedPulse(LED_REACTION_PEAK, 1, now);

  if (!reactionIsBusy() && userReactionCanStart(now)) {
    startConfusedReaction(now);
  } else {
    pendingConfusedVisual = true;
  }
}

// ============================================================
// DEMO STAGE MACHINE
// ============================================================

void updateStageByTime(uint32_t now) {
  if (!demoStarted || completionFlag) return;

  uint32_t elapsed = getActiveElapsedMs(now);

  if (elapsed >= TOTAL_DEMO_MS) {
    completionFlag = true;
    demoClockRunning = false;
    savedActiveMs = TOTAL_DEMO_MS;
    currentStage = 3;
    stageCareMask = 0x07;
    visualCredits = 9;

    triggerRingPulse(now);
    pendingUnlockMask = 0;  // no stale Stage 2/3 animation after completion
    pendingCompletion = true;
    savePersistentState(now);
    return;
  }

  uint8_t targetStage = 1;
  if (elapsed >= 2UL * STAGE_MS) {
    targetStage = 3;
  } else if (elapsed >= STAGE_MS) {
    targetStage = 2;
  }

  if (targetStage > currentStage) {
    currentStage = targetStage;

    // Guarantee the ring reaches the previous stage boundary.
    // Stage 2 starts at 3/9, Stage 3 starts at 6/9.
    uint8_t stageBase = (currentStage - 1) * 3;
    if (visualCredits < stageBase) {
      visualCredits = stageBase;
    }

    stageCareMask = 0;
    resetHungerRequestForStage(now);
    resetPetRequestForStage(now);

    if (currentStage == 2) {
      pendingUnlockMask |= PENDING_UNLOCK_STAGE2;
    } else if (currentStage == 3) {
      pendingUnlockMask |= PENDING_UNLOCK_STAGE3;
    }

    triggerRingPulse(now);
    chooseIdleLook(now);
    savePersistentState(now);

    Serial.println();
    Serial.print("TIME GATE -> STAGE ");
    Serial.println(currentStage);
    printProgressState();
  }
}

void preemptSleepForSystemReaction(uint32_t now) {
  if (reaction != R_SLEEP) return;

  // System events are truly higher priority than persistent sleep. If the
  // physical SLEEP tag is still present, resume sleep after all system events.
  bool resumeSleepAfterSystem = sleepTagPresent;

  reaction = R_NONE;
  sleepWakeStart = 0;
  foodBlinkTriggered = false;
  blinkState = BLINK_OPEN;
  blinkAmount = 0.0f;

  if (resumeSleepAfterSystem) {
    pendingSleepVisual = true;
  }

  Serial.println("SLEEP VISUAL PREEMPTED BY SYSTEM EVENT");
}

void servicePendingSystemReaction(uint32_t now) {
  if (reaction == R_SLEEP && (pendingCompletion || pendingUnlockMask != 0)) {
    preemptSleepForSystemReaction(now);
  }

  if (reactionIsBusy()) return;

  if (pendingCompletion) {
    pendingCompletion = false;
    pendingUnlockMask = 0;
    startCompletionReaction(now);
    return;
  }

  // Preserve both unlock events if a long SLEEP state spans multiple time gates.
  if (pendingUnlockMask & PENDING_UNLOCK_STAGE2) {
    pendingUnlockMask &= ~PENDING_UNLOCK_STAGE2;
    startUnlockReaction(2, now);
    return;
  }

  if (pendingUnlockMask & PENDING_UNLOCK_STAGE3) {
    pendingUnlockMask &= ~PENDING_UNLOCK_STAGE3;
    startUnlockReaction(3, now);
  }
}

void servicePendingUserReaction(uint32_t now) {
  if (reactionIsBusy()) return;
  if (!userReactionCanStart(now)) return;

  // SLEEP is the highest-priority user state, but only start it while the
  // physical tag is still considered present.
  if (pendingSleepVisual) {
    pendingSleepVisual = false;
    if (sleepTagPresent) {
      startSleepReaction(now);
      return;
    }
  }

  // RFID feedback is explicit, then PET, then friendly unknown-card feedback.
  if (pendingFoodVisual != 0) {
    pendingFoodVisual = 0;
    startFoodReaction(now);
    return;
  }

  if (pendingPetVisual) {
    pendingPetVisual = false;
    startPetReaction(now);
    return;
  }

  if (pendingConfusedVisual) {
    pendingConfusedVisual = false;
    startConfusedReaction(now);
  }
}

void updateDemoClock(uint32_t now) {
  if (!demoStarted || completionFlag) {
    return;
  }

  if (demoClockRunning) {
    if ((now - lastInteractionMs) >= INACTIVITY_PAUSE_MS) {
      savedActiveMs = getActiveElapsedMs(now);
      demoClockRunning = false;
      activeRunStartMs = 0;
      savePersistentState(now);

      Serial.println("DEMO CLOCK PAUSED - waiting for interaction");
    }
  }

  updateStageByTime(now);

  uint32_t elapsed = getActiveElapsedMs(now);

  if (demoClockRunning && (elapsed - lastNvsCheckpointElapsed) >= NVS_CHECKPOINT_MS) {
    savePersistentState(now);
    lastNvsCheckpointElapsed = elapsed;
  }
}

// ============================================================
// REACTION UPDATE
// ============================================================

void finishReaction(uint32_t now) {
  if (reaction == R_SLEEP) {
    sleepTagPresent = false;
    sleepWakeStart = 0;
  }

  reaction = R_NONE;
  foodBlinkTriggered = false;

  // Prevent expired visual timers from firing immediately after Wake or
  // another long reaction. Both blink and autonomous personality get a fresh,
  // unpredictable delay after every user/system reaction.
  beginReactionSettle(now);
  scheduleNextAutonomous(now);
}

void updateReaction(uint32_t now) {
  if (reaction == R_NONE) {
      return;
  }

  uint32_t elapsed = now - reactionStart;

  switch (reaction) {
    case R_PET:
      // Long enough to be unmistakable, but still quick enough for repeated play.
      if (elapsed >= 4800) finishReaction(now);
      break;

    case R_FOOD:
      if (!foodBlinkTriggered && elapsed >= 450) {
        startBlink(now);
        foodBlinkTriggered = true;
      }
      if (elapsed >= 3350) finishReaction(now);
      break;

    case R_SLEEP:
      // Persistent state: never auto-finish while the SLEEP tag is present.
      if (!sleepTagPresent) {
        if (sleepWakeStart == 0) {
          sleepWakeStart = now;
        }

        if ((now - sleepWakeStart) >= SLEEP_WAKE_MS) {
          finishReaction(now);
          chooseIdleLook(now);
        }
      }
      break;

    case R_CONFUSED:
      if (elapsed >= 1400) finishReaction(now);
      break;

    case R_UNLOCK2:
      if (elapsed >= 1800) finishReaction(now);
      break;

    case R_UNLOCK3:
      if (elapsed >= 2200) finishReaction(now);
      break;

    case R_COMPLETE:
      if (elapsed >= 4300) finishReaction(now);
      break;

    default:
      finishReaction(now);
      break;
  }
}

// ============================================================
// BLINK UPDATE
// Auto blink runs only when no atomic reaction is active.
// Food may call startBlink() explicitly.
// ============================================================

void updateBlink(uint32_t now) {
  switch (blinkState) {
    case BLINK_OPEN:
      blinkAmount = 0.0f;

      if (reaction == R_NONE &&
          autonomousState == AUTO_NONE &&
          (int32_t)(now - nextBlink) >= 0) {
        startBlink(now);
        scheduleNextBlink(now);
      }
      break;

    case BLINK_CLOSING: {
      float p = (float)(now - blinkStart) / (float)BLINK_CLOSE_MS;
      if (p >= 1.0f) {
        blinkAmount = 1.0f;
        blinkState = BLINK_CLOSED;
        blinkStart = now;
      } else {
        blinkAmount = smoothStep(p);
      }
      break;
    }

    case BLINK_CLOSED:
      blinkAmount = 1.0f;
      if ((now - blinkStart) >= BLINK_HOLD_MS) {
        blinkState = BLINK_OPENING;
        blinkStart = now;
      }
      break;

    case BLINK_OPENING: {
      float p = (float)(now - blinkStart) / (float)BLINK_OPEN_MS;
      if (p >= 1.0f) {
        blinkAmount = 0.0f;
        blinkState = BLINK_OPEN;
      } else {
        blinkAmount = 1.0f - smoothStep(p);
      }
      break;
    }
  }
}

// ============================================================
// EYE TARGETS FOR ALL STATES
// ============================================================

void updateEyeTargets(uint32_t now) {
  happyTarget = 0.0f;
  surpriseTarget = 0.0f;
  sleepCloseTarget = 0.0f;
  specialGlowTarget = 0.0f;

  // Default: alive but calm idle.
  leftEye.targetX = idleX;
  leftEye.targetY = idleY;
  rightEye.targetX = idleX;
  rightEye.targetY = idleY;

  if (reaction == R_NONE) {
    // Stage identity remains present beneath the autonomous personality layer.
    if (currentStage == 3) {
      float tiny = sinf(now * 0.0017f) * 0.7f;
      leftEye.targetY += tiny;
      rightEye.targetY += tiny;
      specialGlowTarget = 0.16f;
    } else if (currentStage == 2) {
      specialGlowTarget = 0.08f;
    }

    if (autonomousState != AUTO_NONE) {
      applyAutonomousEyeTargets(now);
    }

    if (petRequestPromptActive) {
      applyPetRequestEyeCue(now);
    }
    return;
  }

  uint32_t elapsed = now - reactionStart;

  // ----------------------------------------------------------
  // PET: unmistakable affectionate / child-like up-look.
  // Compared with IDLE, both eyes move much higher and further inward,
  // open slightly wider, then make a tiny soft bob while holding the pose.
  // ----------------------------------------------------------
  if (reaction == R_PET) {
    // Fast but smooth lift so the child gets immediate visual confirmation.
    float enter = smoothStep(clamp01((float)elapsed / 560.0f));

    // Keep the expression stable, then return softly to IDLE.
    float exitBlend = 1.0f;
    if (elapsed > 3850) {
      exitBlend = 1.0f - smoothStep(clamp01((float)(elapsed - 3850) / 950.0f));
    }

    float affection = enter * exitBlend;

    // Tender lower-lid lift without turning the eyes into closed happy arcs.
    happyTarget = 0.24f * affection;

    // Slightly larger/rounder eyes plus a stronger soft glow.
    float warmPulse = 0.5f + 0.5f * sinf(elapsed * 0.0038f);
    surpriseTarget = (0.52f + 0.08f * warmPulse) * affection;
    specialGlowTarget = (0.64f + 0.12f * warmPulse) * affection;

    // A visible upward overshoot at the beginning makes PET different from
    // the larger playful IDLE movement range.
    float liftPhase = clamp01((float)elapsed / 900.0f);
    float liftOvershoot = sinf(liftPhase * PI) * 3.8f;

    // Very small living motion while the affectionate pose is held.
    float tinyInward = sinf(elapsed * 0.0031f) * 1.15f;
    float softBob = sinf(elapsed * 0.0020f) * 0.90f;

    // Strong inward/upward gaze. IDLE is about Y >= -10; PET is near -29.
    float petLeftX = +13.0f + tinyInward;
    float petRightX = -13.0f - tinyInward;
    float petY = -29.0f - liftOvershoot + softBob;

    leftEye.targetX = idleX * (1.0f - affection) + petLeftX * affection;
    rightEye.targetX = idleX * (1.0f - affection) + petRightX * affection;
    leftEye.targetY = idleY * (1.0f - affection) + petY * affection;
    rightEye.targetY = idleY * (1.0f - affection) + petY * affection;
    return;
  }

  // ----------------------------------------------------------
  // FOOD: notice -> eat -> satisfied
  // ----------------------------------------------------------
  if (reaction == R_FOOD) {
    if (elapsed < 500) {
      surpriseTarget = 1.0f;
      leftEye.targetX = +4.0f;
      rightEye.targetX = -4.0f;
      leftEye.targetY = +13.0f;
      rightEye.targetY = +13.0f;
    } else if (elapsed < 1950) {
      float bounce = sinf((elapsed - 500) * 0.0090f) * 1.8f;
      leftEye.targetX = +3.0f;
      rightEye.targetX = -3.0f;
      leftEye.targetY = +8.0f + bounce;
      rightEye.targetY = +8.0f + bounce;
    } else {
      happyTarget = 1.0f;
      specialGlowTarget = 0.22f;
      leftEye.targetX = +5.0f;
      rightEye.targetX = -5.0f;
      leftEye.targetY = -6.0f;
      rightEye.targetY = -6.0f;
    }
    return;
  }

  // ----------------------------------------------------------
  // SLEEP: natural drowsy settling -> persistent closed state -> wake on tag removal
  // ----------------------------------------------------------
  if (reaction == R_SLEEP) {
    if (sleepTagPresent) {
      // The gaze becomes slower and settles before the eyelids fully close.
      float calm = smoothStep(clamp01((float)elapsed / 2600.0f));
      float sleepySway = sinf(elapsed * 0.0032f) * 2.2f * (1.0f - calm);

      leftEye.targetX = sleepySway;
      rightEye.targetX = sleepySway;
      leftEye.targetY = 1.0f + 2.0f * calm;
      rightEye.targetY = 1.0f + 2.0f * calm;

      // Drowsy eyelid motion: close a little, resist/reopen slightly,
      // then become heavy and finally close. This avoids a mechanical
      // linear "shutter" look.
      if (elapsed < 450) {
        sleepCloseTarget = 0.0f;
      } else if (elapsed < 1150) {
        float p = smoothStep((float)(elapsed - 450) / 700.0f);
        sleepCloseTarget = 0.38f * p;
      } else if (elapsed < 1450) {
        float p = smoothStep((float)(elapsed - 1150) / 300.0f);
        sleepCloseTarget = 0.38f + (0.22f - 0.38f) * p;
      } else if (elapsed < 2350) {
        float p = smoothStep((float)(elapsed - 1450) / 900.0f);
        sleepCloseTarget = 0.22f + (0.72f - 0.22f) * p;
      } else if (elapsed < 2650) {
        float p = smoothStep((float)(elapsed - 2350) / 300.0f);
        sleepCloseTarget = 0.72f + (0.60f - 0.72f) * p;
      } else if (elapsed < 3600) {
        float p = smoothStep((float)(elapsed - 2650) / 950.0f);
        sleepCloseTarget = 0.60f + (1.0f - 0.60f) * p;
      } else {
        sleepCloseTarget = 1.0f;
      }
    } else {
      // Tag is gone: open softly and return to the living idle state.
      uint32_t wakeElapsed = (sleepWakeStart == 0) ? 0 : (now - sleepWakeStart);
      float p = smoothStep(clamp01((float)wakeElapsed / (float)SLEEP_WAKE_MS));

      sleepCloseTarget = 1.0f - p;
      leftEye.targetX = 0.0f;
      rightEye.targetX = 0.0f;
      leftEye.targetY = 3.0f * (1.0f - p);
      rightEye.targetY = 3.0f * (1.0f - p);
    }
    return;
  }

  // ----------------------------------------------------------
  // Friendly confused / curious
  // ----------------------------------------------------------
  if (reaction == R_CONFUSED) {
    // Slow shared side-to-side curiosity instead of high-frequency opposing
    // shake. A small inward offset keeps the expression friendly and focused.
    float curiousSway = sinf(elapsed * 0.0085f) * 6.0f;
    leftEye.targetX = curiousSway + 2.0f;
    rightEye.targetX = curiousSway - 2.0f;
    leftEye.targetY = -1.0f;
    rightEye.targetY = -1.0f;
    surpriseTarget = 0.28f;
    specialGlowTarget = 0.10f;
    return;
  }

  // ----------------------------------------------------------
  // STAGE 2 UNLOCK
  // ----------------------------------------------------------
  if (reaction == R_UNLOCK2) {
    float p = clamp01((float)elapsed / 1800.0f);
    happyTarget = 0.55f;
    surpriseTarget = 0.35f * sinf(p * PI);
    specialGlowTarget = 0.75f;

    float sway = sinf(p * PI * 3.0f) * (1.0f - p) * 7.0f;
    leftEye.targetX = -sway;
    rightEye.targetX = +sway;
    leftEye.targetY = -4.0f;
    rightEye.targetY = -4.0f;
    return;
  }

  // ----------------------------------------------------------
  // STAGE 3 UNLOCK
  // ----------------------------------------------------------
  if (reaction == R_UNLOCK3) {
    float p = clamp01((float)elapsed / 2200.0f);
    happyTarget = 0.80f;
    surpriseTarget = 0.45f * sinf(p * PI);
    specialGlowTarget = 1.0f;

    float bounce = fabsf(sinf(p * PI * 3.0f)) * -6.0f;
    leftEye.targetX = +4.0f;
    rightEye.targetX = -4.0f;
    leftEye.targetY = bounce;
    rightEye.targetY = bounce;
    return;
  }

  // ----------------------------------------------------------
  // COMPLETION CELEBRATION
  // ----------------------------------------------------------
  if (reaction == R_COMPLETE) {
    float p = clamp01((float)elapsed / 4300.0f);
    happyTarget = 1.0f;
    specialGlowTarget = 1.0f;

    float bounce = sinf(elapsed * 0.012f) * 5.0f * (1.0f - 0.35f * p);
    leftEye.targetX = +5.0f;
    rightEye.targetX = -5.0f;
    leftEye.targetY = -7.0f + bounce;
    rightEye.targetY = -7.0f + bounce;
    return;
  }
}

void updateEyeDynamics(float dt) {
  const float gazeSpeed = 5.4f;

  leftEye.x = smoothFollow(leftEye.x, leftEye.targetX, dt, gazeSpeed);
  leftEye.y = smoothFollow(leftEye.y, leftEye.targetY, dt, gazeSpeed);

  rightEye.x = smoothFollow(rightEye.x, rightEye.targetX, dt, gazeSpeed);
  rightEye.y = smoothFollow(rightEye.y, rightEye.targetY, dt, gazeSpeed);

  happyBlend = smoothFollow(happyBlend, happyTarget, dt, 7.0f);
  surpriseBlend = smoothFollow(surpriseBlend, surpriseTarget, dt, 7.0f);
  sleepClose = smoothFollow(sleepClose, sleepCloseTarget, dt, 8.0f);
  specialGlow = smoothFollow(specialGlow, specialGlowTarget, dt, 6.0f);
}

// ============================================================
// DRAW Z WITHOUT FONT
// ============================================================

void drawZSymbol(int x, int y, int s, uint16_t color) {
  frame.drawLine(x, y, x + s, y, color);
  frame.drawLine(x + s, y, x, y + s, color);
  frame.drawLine(x, y + s, x + s, y + s, color);
}

void drawSleepGraphics(bool leftSide, uint32_t now) {
  if (leftSide || reaction != R_SLEEP || !sleepTagPresent) return;

  uint32_t elapsed = now - reactionStart;
  if (elapsed < 3200) return;

  // Keep the Z marks gently floating for as long as the tag remains present.
  float t = (float)(elapsed - 3200) / 1000.0f;
  float floatY = fmodf(t * 9.0f, 18.0f);

  uint16_t c = blend565(C_SLEEP_Z, C_WHITE, 0.15f + 0.15f * sinf(now * 0.006f));

  drawZSymbol(142, 58 - (int)floatY, 10, c);
  drawZSymbol(160, 38 - (int)(floatY * 0.7f), 7, c);
}

// ============================================================
// DRAW PROGRESS RING
// ============================================================

uint16_t getRingColor() {
  // During an unlock, show the color of the unlock being presented even if
  // currentStage has already advanced further while another state was active.
  if (reaction == R_UNLOCK2) return C_RING_S2;
  if (reaction == R_UNLOCK3 || reaction == R_COMPLETE || completionFlag) return C_RING_S3;
  if (currentStage == 1) return C_RING_S1;
  if (currentStage == 2) return C_RING_S2;
  return C_RING_S3;
}

float getRingPulseAmount(uint32_t now) {
  uint32_t elapsed = now - ringPulseStart;
  if (ringPulseStart == 0 || elapsed >= RING_PULSE_MS) return 0.0f;

  float p = (float)elapsed / (float)RING_PULSE_MS;
  return sinf(p * PI);
}

void drawProgressRing(uint32_t now) {
  const int segments = 120;      // 3 degrees per segment
  const float segmentDeg = 360.0f / (float)segments;
  const float radius = 96.0f;
  const float centerX = 100.0f;
  const float centerY = 100.0f;

  float progress = (float)visualCredits / (float)TOTAL_VISUAL_CREDITS;
  progress = clamp01(progress);

  // Symmetric fill: start at 12 o'clock and grow equally to the left and right.
  // At 50%, 90 degrees are filled on each side. At 100%, the ring is complete.
  float halfSpanDeg = progress * 180.0f;

  float pulse = getRingPulseAmount(now);
  uint16_t ringColor = blend565(getRingColor(), C_WHITE, pulse * 0.55f);

  int thickness = 3;
  if (pulse > 0.35f) thickness = 4;

  for (int i = 0; i < segments; i++) {
    float startDeg = -90.0f + i * segmentDeg;
    float endDeg = startDeg + segmentDeg;
    float midDeg = startDeg + segmentDeg * 0.5f;

    float a1 = startDeg * DEG_TO_RAD;
    float a2 = endDeg * DEG_TO_RAD;

    // Thin dark track.
    int tx1 = (int)(centerX + cosf(a1) * radius);
    int ty1 = (int)(centerY + sinf(a1) * radius);
    int tx2 = (int)(centerX + cosf(a2) * radius);
    int ty2 = (int)(centerY + sinf(a2) * radius);
    frame.drawLine(tx1, ty1, tx2, ty2, C_RING_TRACK);

    // Angular distance from the 12 o'clock origin, normalized to -180..+180.
    float delta = midDeg - (-90.0f);
    while (delta > 180.0f) delta -= 360.0f;
    while (delta < -180.0f) delta += 360.0f;

    if (fabsf(delta) <= halfSpanDeg) {
      for (int t = 0; t < thickness; t++) {
        float r = radius - (float)t;
        int x1 = (int)(centerX + cosf(a1) * r);
        int y1 = (int)(centerY + sinf(a1) * r);
        int x2 = (int)(centerX + cosf(a2) * r);
        int y2 = (int)(centerY + sinf(a2) * r);
        frame.drawLine(x1, y1, x2, y2, ringColor);
      }
    }
  }
}

// ============================================================
// COMPLETION SPARKLES
// ============================================================

void drawCompletionSparkles(bool leftSide, uint32_t now) {
  if (reaction != R_COMPLETE) return;

  uint32_t elapsed = now - reactionStart;
  float phase = elapsed * 0.008f;

  for (int i = 0; i < 6; i++) {
    float a = phase + i * (TWO_PI / 6.0f) + (leftSide ? 0.0f : 0.35f);
    float rr = 78.0f + sinf(phase * 0.7f + i) * 7.0f;
    int x = (int)(SCREEN_CX + cosf(a) * rr);
    int y = (int)(SCREEN_CY + sinf(a) * rr);
    int r = 1 + ((i + (elapsed / 180)) % 2);
    frame.fillCircle(x, y, r, C_SPARK);
  }
}


// ============================================================
// HUNGER FOOD OVERLAY
//
// The original yellow hunger sticker is removed. During a Hunger request the
// normal eye renderer continues unchanged. A small animated food cue is drawn
// only in the lower part of each display:
//   - LEFT display: chicken drumstick
//   - RIGHT display: the same chicken drumstick
// Both use gentle bob/wiggle motion and never replace the eye.
// ============================================================

void drawChickenDrumstick(int cx, int cy, float phase) {
  uint16_t meatDark = rgb565(145, 63, 28);
  uint16_t meat = rgb565(205, 93, 39);
  uint16_t meatHi = rgb565(238, 132, 58);
  uint16_t bone = rgb565(240, 224, 190);
  uint16_t boneHi = rgb565(255, 247, 224);

  int wiggle = (int)(sinf(phase * 1.7f) * 2.0f);
  int bob = (int)(sinf(phase * 2.2f) * 2.0f);
  cx += wiggle;
  cy += bob;

  // Meat head, angled slightly toward the center.
  frame.fillCircle(cx - 8, cy - 4, 12, meatDark);
  frame.fillCircle(cx + 2, cy - 8, 13, meatDark);
  frame.fillCircle(cx - 7, cy - 5, 10, meat);
  frame.fillCircle(cx + 2, cy - 8, 11, meat);
  frame.fillTriangle(cx - 13, cy + 1, cx + 10, cy - 16, cx + 12, cy + 7, meat);
  frame.fillCircle(cx - 4, cy - 10, 4, meatHi);

  // Bone shaft. Multiple lines make it readable at 200x200.
  for (int o = -2; o <= 2; o++) {
    frame.drawLine(cx + 7, cy + 5 + o, cx + 25, cy + 15 + o, bone);
  }
  frame.drawLine(cx + 8, cy + 4, cx + 24, cy + 13, boneHi);

  // Bone knobs.
  frame.fillCircle(cx + 28, cy + 15, 5, bone);
  frame.fillCircle(cx + 25, cy + 19, 5, bone);
  frame.fillCircle(cx + 27, cy + 14, 2, boneHi);
}



void drawHungerFoodOverlay(bool leftSide, uint32_t now) {
  if (!hungerPromptActive) return;

  uint32_t elapsed = now - hungerPromptStart;
  float phase = (float)elapsed / 1000.0f;

  // Keep the cue low enough not to replace the eye, but high enough to stay
  // inside the 200x200 canvas and inside the progress ring.
  // Same Hunger cue on both displays, as requested.
  // A tiny phase offset prevents the pair from looking mechanically mirrored.
  float displayPhase = phase + (leftSide ? 0.0f : 0.18f);
  drawChickenDrumstick(100, 164, displayPhase);

  // Small attention pulse under the icon, still below the eye.
  float pulse = 0.5f + 0.5f * sinf(phase * 4.5f);
  uint16_t cue = blend565(getRingColor(), C_WHITE, 0.25f + pulse * 0.30f);
  int r = 2 + (int)(pulse * 2.0f);
  frame.fillCircle(100, 188, r, cue);
}


void drawPetRequestHandIcon(int cx, int cy, float phase) {
  uint16_t hand = rgb565(250, 205, 165);
  uint16_t handHi = rgb565(255, 231, 205);
  uint16_t handEdge = rgb565(171, 112, 88);
  uint16_t motion = blend565(C_BLUSH, C_WHITE, 0.30f);

  int sway = (int)(sinf(phase * 2.5f) * 6.0f);
  int bob = (int)(sinf(phase * 1.7f + 0.6f) * 1.5f);
  cx += sway;
  cy += bob;

  // Palm.
  frame.fillRoundRect(cx - 9, cy - 4, 22, 19, 7, handEdge);
  frame.fillRoundRect(cx - 8, cy - 5, 20, 18, 7, hand);

  // Four soft fingers tilted upward; line thickness is built from neighbors.
  for (int o = 0; o < 2; o++) {
    frame.drawLine(cx - 7 + o, cy - 3, cx - 10 + o, cy - 16, hand);
    frame.drawLine(cx - 2 + o, cy - 5, cx - 3 + o, cy - 20, hand);
    frame.drawLine(cx + 3 + o, cy - 5, cx + 4 + o, cy - 19, hand);
    frame.drawLine(cx + 8 + o, cy - 3, cx + 11 + o, cy - 14, hand);
  }

  // Fingertips and thumb.
  frame.fillCircle(cx - 10, cy - 16, 2, handHi);
  frame.fillCircle(cx - 3, cy - 20, 2, handHi);
  frame.fillCircle(cx + 4, cy - 19, 2, handHi);
  frame.fillCircle(cx + 11, cy - 14, 2, handHi);
  frame.fillCircle(cx + 13, cy + 3, 5, hand);
  frame.drawLine(cx + 11, cy + 1, cx + 16, cy - 4, handEdge);

  // Wrist.
  frame.fillRoundRect(cx - 4, cy + 11, 12, 9, 4, hand);

  // Two curved-looking stroke trails, approximated by short segments.
  int trailShift = (sway > 0) ? -4 : 4;
  frame.drawLine(cx - 23 + trailShift, cy - 6, cx - 18 + trailShift, cy - 10, motion);
  frame.drawLine(cx - 18 + trailShift, cy - 10, cx - 12 + trailShift, cy - 11, motion);
  frame.drawLine(cx + 16 + trailShift, cy - 10, cx + 22 + trailShift, cy - 7, motion);
  frame.drawLine(cx + 22 + trailShift, cy - 7, cx + 25 + trailShift, cy - 3, motion);
}

void drawPetRequestOverlay(bool leftSide, uint32_t now) {
  if (!petRequestPromptActive || reaction != R_NONE) return;

  uint32_t elapsed = now - petRequestPromptStart;
  float phase = (float)elapsed / 1000.0f + (leftSide ? 0.0f : 0.22f);

  // Keep the hand cue in the lower area, distinct from the eye identity.
  drawPetRequestHandIcon(100, 166, phase);

  // Very small blush pulse helps read the cue as affectionate rather than a
  // generic touch instruction. No heart icon is used.
  float pulse = 0.5f + 0.5f * sinf(phase * 3.2f);
  int bx = leftSide ? 36 : 164;
  int br = 2 + (int)(pulse * 2.0f);
  frame.fillCircle(bx, 134, br, C_BLUSH);
}

// ============================================================
// DRAW FANTASY EYE
// ============================================================

void drawFantasyEye(bool leftSide, uint32_t now) {
  EyeMotion &eye = leftSide ? leftEye : rightEye;

  // Tiny living micro drift.
  float microX = sinf(now * 0.0017f + (leftSide ? 0.0f : 0.35f)) * 0.45f;
  float microY = sinf(now * 0.00115f + (leftSide ? 0.2f : 0.5f)) * 0.35f;

  float cx = SCREEN_CX + eye.x + microX;
  float cy = EYE_BASE_Y + eye.y + microY;

  float w = 100.0f + surpriseBlend * 8.0f;
  float h = 58.0f + surpriseBlend * 14.0f;

  // Unlock/completion makes eye breathe slightly larger.
  if (reaction == R_UNLOCK2 || reaction == R_UNLOCK3 || reaction == R_COMPLETE) {
    float pulse = 0.5f + 0.5f * sinf(now * 0.010f);
    w += 4.0f * pulse;
    h += 3.0f * pulse;
  }

  float effectiveBlink = blinkAmount;
  float personalityLid = autonomousLidAmount(leftSide, now);
  if (personalityLid > effectiveBlink) effectiveBlink = personalityLid;

  float requestLid = petRequestLidAmount(now);
  if (requestLid > effectiveBlink) effectiveBlink = requestLid;

  float closeAmount = effectiveBlink;

  if (sleepClose > effectiveBlink && sleepClose > 0.001f) {
    // Sleep closure is top-lid dominant. Keep the lower edge almost fixed
    // while the upper lid settles downward. This reads more naturally than
    // shrinking the eye equally from top and bottom.
    float originalH = h;
    h *= (1.0f - sleepClose * 0.93f);
    if (h < 4.0f) h = 4.0f;
    cy += (originalH - h) * 0.46f;
    closeAmount = sleepClose;
  } else {
    // Normal blink is also top-lid dominant. Keep the lower edge nearly fixed
    // instead of collapsing the eye equally from the top and bottom.
    float originalH = h;
    h *= (1.0f - effectiveBlink * 0.93f);
    if (h < 4.0f) h = 4.0f;
    cy += (originalH - h) * 0.46f;
  }

  // PET has its own slow, shallow eyelid soften. It is intentionally much
  // gentler than a normal blink: the eyes stay open, but briefly "melt" into
  // the affectionate pose, which makes the reaction more noticeable.
  if (reaction == R_PET && sleepClose < 0.01f && effectiveBlink < 0.01f) {
    uint32_t petElapsed = now - reactionStart;
    float petSoftClose = 0.0f;

    if (petElapsed >= 1550 && petElapsed < 2350) {
      float p = (float)(petElapsed - 1550) / 800.0f;
      petSoftClose = sinf(clamp01(p) * PI) * 0.13f;
    } else if (petElapsed >= 2900 && petElapsed < 3500) {
      float p = (float)(petElapsed - 2900) / 600.0f;
      petSoftClose = sinf(clamp01(p) * PI) * 0.07f;
    }

    if (petSoftClose > 0.001f) {
      float originalH = h;
      h *= (1.0f - petSoftClose);
      cy += (originalH - h) * 0.20f;
    }
  }

  int iw = (int)w;
  int ih = (int)h;
  int ix = (int)(cx - w / 2.0f);
  int iy = (int)(cy - h / 2.0f);

  int radius = (int)(h / 2.0f);
  if (radius > 22) radius = 22;
  if (radius < 2) radius = 2;

  uint16_t glowColor = blend565(C_EYE_GLOW, C_EYE, specialGlow * 0.50f);
  int glowPad = 3 + (int)(specialGlow * 3.0f);

  frame.fillRoundRect(
    ix - glowPad,
    iy - glowPad,
    iw + glowPad * 2,
    ih + glowPad * 2,
    radius + glowPad,
    glowColor
  );

  frame.fillRoundRect(ix, iy, iw, ih, radius, C_EYE);

  if (closeAmount < 0.40f) {
    frame.fillRoundRect(ix + 17, iy + 10, 20, 4, 2, C_EYE_SHINE);
  }

  // Happy / affectionate eye: raise lower eyelid.
  if (happyBlend > 0.02f) {
    int cover = (int)(h * 0.39f * happyBlend);
    frame.fillRect(ix - 2, iy + ih - cover, iw + 4, cover + 4, C_BLACK);

    if (happyBlend > 0.50f) {
      int bx = leftSide ? 35 : 165;
      frame.fillCircle(bx, 132, 4, C_BLUSH);
    }
  }

  // PET adds a visible cheek blush and a small inner sparkle.
  // These accents make the affectionate state clearly different from IDLE.
  if (reaction == R_PET && closeAmount < 0.45f) {
    uint32_t petElapsed = now - reactionStart;
    float petIn = smoothStep(clamp01((float)petElapsed / 700.0f));
    float petOut = 1.0f;
    if (petElapsed > 3850) {
      petOut = 1.0f - smoothStep(clamp01((float)(petElapsed - 3850) / 950.0f));
    }

    float petStrength = petIn * petOut;
    if (petStrength > 0.15f) {
      int bx = leftSide ? 34 : 166;
      int blushR = (petStrength > 0.70f) ? 5 : 4;
      frame.fillCircle(bx, 132, blushR, C_BLUSH);
      frame.fillCircle(bx + (leftSide ? 8 : -8), 135, 2, C_BLUSH);

      // Sparkle is on the inner/top side of each eye, reinforcing the
      // "looking up at you" expression.
      int sx = leftSide ? (ix + iw - 24) : (ix + 24);
      int sy = iy + 12;
      int sr = (petStrength > 0.65f) ? 4 : 3;
      frame.fillCircle(sx, sy, sr, C_WHITE);
      frame.fillCircle(sx + (leftSide ? -6 : 6), sy + 7, 1, C_WHITE);
    }
  }

}

// ============================================================
// DISPLAY PUSH
// ============================================================

void pushLeft() {
  uint16_t *buffer = frame.getBuffer();

  leftDisplay.startWrite();
  leftDisplay.setAddrWindow(FRAME_X, FRAME_Y, FRAME_W, FRAME_H);
  leftDisplay.writePixels(buffer, FRAME_W * FRAME_H);
  leftDisplay.endWrite();
}

void pushRight() {
  uint16_t *buffer = frame.getBuffer();

  rightDisplay.startWrite();
  rightDisplay.setAddrWindow(FRAME_X, FRAME_Y, FRAME_W, FRAME_H);
  rightDisplay.writePixels(buffer, FRAME_W * FRAME_H);
  rightDisplay.endWrite();
}

void renderDisplay(bool leftSide, uint32_t now) {
  frame.fillScreen(C_BLACK);

  // Hunger no longer replaces the face. The normal eye keeps its full state
  // machine (idle, Blink, Wink, Smile, Play and user reactions).
  drawFantasyEye(leftSide, now);
  drawSleepGraphics(leftSide, now);
  drawCompletionSparkles(leftSide, now);

  if (hungerPromptActive) {
    drawHungerFoodOverlay(leftSide, now);
  }

  if (petRequestPromptActive) {
    drawPetRequestOverlay(leftSide, now);
  }

  drawProgressRing(now);

  if (leftSide) {
    pushLeft();
  } else {
    pushRight();
  }
}

// ============================================================
// MPR121 CAPACITIVE / PROXIMITY PET GESTURE
//
// Current hardware rule:
//   - E0 / E6 / E11 are the three PET electrodes.
//   - If ANY TWO OR MORE of those electrodes are currently touched, PET must
//     trigger after a very short stability confirmation.
//   - One electrode alone never triggers PET.
//   - Order, accumulated 1-second session time, stale-session history and
//     full-release state are NOT part of PET qualification anymore.
//   - After one trigger, the same continuous >=2-pad hold is latched so it does
//     not fire every 20 ms. PET re-arms after the live touched count stays below
//     two for PET_REARM_CLEAR_MS. One residual/stuck YES electrode is therefore
//     explicitly tolerated.
//   - PET remains disabled while persistent SLEEP is active.
// ============================================================

const uint8_t PET_ELECTRODE_COUNT = 3;
const uint8_t PET_ELECTRODES[PET_ELECTRODE_COUNT] = { 0, 6, 11 };
const uint16_t PET_E0_MASK  = (1U << 0);
const uint16_t PET_E6_MASK  = (1U << 6);
const uint16_t PET_E11_MASK = (1U << 11);
const uint16_t PET_ALL_MASK = PET_E0_MASK | PET_E6_MASK | PET_E11_MASK;
const uint16_t PET_ELECTRODE_MASKS[PET_ELECTRODE_COUNT] = {
  PET_E0_MASK, PET_E6_MASK, PET_E11_MASK
};

// MPR121 sensitivity. Lower values = more sensitive.
const uint8_t MPR_TOUCH_THRESHOLD = 6;
const uint8_t MPR_RELEASE_THRESHOLD = 3;

// Require one additional 20 ms poll with >=2 pads before firing. This rejects
// a single noisy sample without making the gesture feel delayed.
const uint32_t PET_TWO_PAD_CONFIRM_MS = 20;
const uint32_t PET_REARM_CLEAR_MS = 220;
const uint32_t PET_EVENT_REFRACTORY_MS = 800;

bool petGestureLocked = false;
bool petTwoPadCandidate = false;
uint32_t petTwoPadSince = 0;
uint32_t petBelowTwoSince = 0;
uint32_t lastPetTriggerAt = 0;
bool hasPetTrigger = false;

uint8_t countPetBits(uint16_t mask) {
  uint8_t count = 0;
  if (mask & PET_E0_MASK) count++;
  if (mask & PET_E6_MASK) count++;
  if (mask & PET_E11_MASK) count++;
  return count;
}

void resetPetDetector() {
  petGestureLocked = false;
  petTwoPadCandidate = false;
  petTwoPadSince = 0;
  petBelowTwoSince = 0;
  hasPetTrigger = false;
  lastPetTriggerAt = 0;
}

bool recalibrateMpr121(uint32_t now) {
  // Manual diagnostic recovery only. Do not auto-recalibrate during normal
  // play because a real hand could be present and become part of the baseline.
  Serial.println("MPR121 RECALIBRATION - KEEP HAND AWAY");
  delay(600);

  if (!mpr.begin(
        0x5A,
        &Wire,
        MPR_TOUCH_THRESHOLD,
        MPR_RELEASE_THRESHOLD,
        true)) {
    Serial.println("MPR121 RECALIBRATION FAILED");
    return false;
  }

  resetPetDetector();
  lastPetTriggerAt = now;

  delay(300);
  Serial.println("MPR121 RECALIBRATION COMPLETE");
  printMprDiagnostics();
  return true;
}

void printPetMask(uint16_t mask, uint32_t stableMs) {
  Serial.print("PET CAPACITIVE: ");

  bool first = true;
  if (mask & PET_E0_MASK) {
    Serial.print("E0");
    first = false;
  }
  if (mask & PET_E6_MASK) {
    if (!first) Serial.print("+");
    Serial.print("E6");
    first = false;
  }
  if (mask & PET_E11_MASK) {
    if (!first) Serial.print("+");
    Serial.print("E11");
  }

  Serial.print(" | stable=");
  Serial.print(stableMs);
  Serial.println(" ms -> PET");
}

// One-shot diagnostic for real hardware calibration.
void printMprDiagnostics() {
  uint16_t hwTouched = mpr.touched() & PET_ALL_MASK;
  Serial.println("---- MPR121 PET E0/E6/E11 ----");
  for (uint8_t i = 0; i < PET_ELECTRODE_COUNT; i++) {
    uint8_t electrode = PET_ELECTRODES[i];
    uint16_t mask = PET_ELECTRODE_MASKS[i];
    uint16_t baseline = mpr.baselineData(electrode);
    uint16_t filtered = mpr.filteredData(electrode);
    int16_t delta = (int16_t)baseline - (int16_t)filtered;

    Serial.print("E");
    Serial.print(electrode);
    Serial.print(" baseline=");
    Serial.print(baseline);
    Serial.print(" filtered=");
    Serial.print(filtered);
    Serial.print(" delta=");
    Serial.print(delta);
    Serial.print(" touched=");
    Serial.println((hwTouched & mask) ? "YES" : "NO");
  }

  Serial.print("thresholds touch/release = ");
  Serial.print(MPR_TOUCH_THRESHOLD);
  Serial.print("/");
  Serial.println(MPR_RELEASE_THRESHOLD);
  Serial.print("MPR121 ECR=0x");
  Serial.println(mpr.readRegister8(MPR121_ECR), HEX);
  Serial.print("touched baseline filter NHDT/NCLT/FDLT (read-only) = ");
  Serial.print(mpr.readRegister8(MPR121_NHDT), HEX);
  Serial.print("/");
  Serial.print(mpr.readRegister8(MPR121_NCLT), HEX);
  Serial.print("/");
  Serial.println(mpr.readRegister8(MPR121_FDLT), HEX);

  Serial.print("pet state: touchedCount=");
  Serial.print(countPetBits(hwTouched));
  Serial.print(" locked=");
  Serial.print(petGestureLocked ? "YES" : "NO");
  Serial.print(" candidate=");
  Serial.print(petTwoPadCandidate ? "YES" : "NO");
  Serial.print(" belowTwoSince=");
  Serial.println(petBelowTwoSince);
  Serial.println("--------------------------");
}

void updateTouch(uint32_t now) {
  uint16_t rawTouched = mpr.touched() & PET_ALL_MASK;
  uint8_t touchedCount = countPetBits(rawTouched);

  // PET is completely disabled during persistent sleep.
  if (reaction == R_SLEEP || pendingSleepVisual) {
    petGestureLocked = false;
    petTwoPadCandidate = false;
    petTwoPadSince = 0;
    petBelowTwoSince = 0;
    return;
  }

  // After firing once, do not repeatedly trigger on the exact same continuous
  // >=2-pad hold. Re-arm as soon as fewer than two pads stay active for a
  // short stable period. A single stuck/residual YES electrode is acceptable.
  if (petGestureLocked) {
    if (touchedCount < 2) {
      if (petBelowTwoSince == 0) petBelowTwoSince = now;

      if ((now - petBelowTwoSince) >= PET_REARM_CLEAR_MS &&
          (!hasPetTrigger || (now - lastPetTriggerAt) >= PET_EVENT_REFRACTORY_MS)) {
        petGestureLocked = false;
        petBelowTwoSince = 0;
        Serial.println("PET REARMED - LIVE TOUCH COUNT BELOW 2");
      }
    } else {
      petBelowTwoSince = 0;
    }
    return;
  }

  // New rule: live 2-of-3 or 3-of-3 is itself the PET qualification.
  if (touchedCount >= 2) {
    if (!petTwoPadCandidate) {
      petTwoPadCandidate = true;
      petTwoPadSince = now;
      return;
    }

    uint32_t stableMs = now - petTwoPadSince;
    if (stableMs >= PET_TWO_PAD_CONFIRM_MS &&
        (!hasPetTrigger || (now - lastPetTriggerAt) >= PET_EVENT_REFRACTORY_MS)) {
      printPetMask(rawTouched, stableMs);
      handlePetEvent(now);

      petGestureLocked = true;
      petTwoPadCandidate = false;
      petTwoPadSince = 0;
      petBelowTwoSince = 0;
      lastPetTriggerAt = now;
      hasPetTrigger = true;
    }
    return;
  }

  // Fewer than two live touched electrodes means there is no PET candidate.
  petTwoPadCandidate = false;
  petTwoPadSince = 0;
}

// ============================================================
// RFID HELPERS / LATCH
// ============================================================

bool rfidLatched = false;
bool rfidLatchedIsSleep = false;
uint32_t rfidNoCardSince = 0;
const uint32_t RFID_REARM_MS = 500;

bool uidMatches(const byte *target, byte targetSize) {
  if (rfid.uid.size != targetSize) return false;

  for (byte i = 0; i < targetSize; i++) {
    if (rfid.uid.uidByte[i] != target[i]) return false;
  }

  return true;
}

void printUid() {
  Serial.print("RFID UID:");
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) Serial.print(" 0");
    else Serial.print(" ");
    Serial.print(rfid.uid.uidByte[i], HEX);
  }
  Serial.println();
}

bool cardStillPresent() {
  byte atqa[2];
  byte atqaSize = sizeof(atqa);

  MFRC522::StatusCode status = rfid.PICC_WakeupA(atqa, &atqaSize);

  return status == MFRC522::STATUS_OK ||
         status == MFRC522::STATUS_COLLISION;
}

void updateRFID(uint32_t now) {
  // First, manage a card that has already been handled.
  if (rfidLatched) {
    if (cardStillPresent()) {
      rfidNoCardSince = 0;

      if (rfidLatchedIsSleep && reaction == R_SLEEP) {
        // A held SLEEP tag is a continuous interaction. Keep sleep alive
        // and keep the active-demo timer from pausing for inactivity.
        sleepTagPresent = true;
        lastInteractionMs = now;
      }
      return;
    }

    if (rfidNoCardSince == 0) {
      rfidNoCardSince = now;
      return;
    }

    if ((now - rfidNoCardSince) >= RFID_REARM_MS) {
      if (rfidLatchedIsSleep) {
        if (reaction == R_SLEEP) {
          beginSleepWake(now);
        } else {
          // SLEEP was waiting behind a system-priority animation but the tag
          // disappeared before sleep actually started.
          sleepTagPresent = false;
          pendingSleepVisual = false;
        }
      }

      rfidLatched = false;
      rfidLatchedIsSleep = false;
      rfidNoCardSince = 0;
      Serial.println("RFID READY");
    }

    return;
  }

  // Keep polling RFID even while a short PET/FOOD animation is active.
  // Event handlers either start the reaction immediately, coalesce a pending
  // visual, or give SLEEP the appropriate priority.
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  Serial.println();
  printUid();

  rfidLatchedIsSleep = false;

  if (uidMatches(FOOD1_UID, FOOD1_UID_SIZE)) {
    handleFoodEvent(1, now);
  } else if (uidMatches(FOOD2_UID, FOOD2_UID_SIZE)) {
    handleFoodEvent(2, now);
  } else if (uidMatches(SLEEP_UID, SLEEP_UID_SIZE)) {
    Serial.println("SLEEP TAG ACCEPTED");
    rfidLatchedIsSleep = true;
    handleSleepEvent(now);
  } else {
    Serial.println("UNKNOWN RFID TAG");
    handleUnknownRfidEvent(now);
  }

  rfidLatched = true;
  rfidNoCardSince = 0;

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ============================================================
// SERIAL TEST / PRESENTATION COMMANDS
// ============================================================

void printDemoStatus(uint32_t now) {
  uint32_t elapsed = getActiveElapsedMs(now);

  Serial.println();
  Serial.println("---------- TANDO STATUS ----------");
  Serial.print("Demo started: ");
  Serial.println(demoStarted ? "YES" : "NO");
  Serial.print("Clock running: ");
  Serial.println(demoClockRunning ? "YES" : "NO / PAUSED");
  Serial.print("Active time: ");
  Serial.print(elapsed / 60000UL);
  Serial.print("m ");
  Serial.print((elapsed / 1000UL) % 60UL);
  Serial.println("s");
  Serial.print("Stage: ");
  Serial.println(currentStage);
  Serial.print("Current-stage care mask: 0x");
  Serial.println(stageCareMask, HEX);
  Serial.print("Progress credits: ");
  Serial.print(visualCredits);
  Serial.println(" / 9");
  Serial.print("Complete: ");
  Serial.println(completionFlag ? "YES" : "NO");
  Serial.print("Autonomous state: ");
  Serial.println(autonomousStateName(autonomousState));
  Serial.print("Hunger requests: stage=");
  Serial.print(hungerRequestStage);
  Serial.print(" completed=");
  Serial.print(hungerRequestsShown);
  Serial.print("/");
  Serial.print(HUNGER_REQUESTS_PER_STAGE);
  Serial.print(" active=");
  Serial.print(hungerPromptActive ? "YES" : "NO");
  Serial.print(" foodSatisfied=");
  Serial.print(hungerNeedSatisfiedThisStage() ? "YES" : "NO");
  Serial.print(" scheduler=");
  Serial.println((!completionFlag &&
                  !hungerNeedSatisfiedThisStage() &&
                  hungerRequestsShown < HUNGER_REQUESTS_PER_STAGE) ? "ENABLED" : "STOPPED");

  Serial.print("PET requests: stage=");
  Serial.print(petRequestStage);
  Serial.print(" completed=");
  Serial.print(petRequestsShown);
  Serial.print("/");
  Serial.print(PET_REQUESTS_PER_STAGE);
  Serial.print(" active=");
  Serial.print(petRequestPromptActive ? "YES" : "NO");
  Serial.print(" petSatisfied=");
  Serial.print(petRequestNeedSatisfiedThisStage() ? "YES" : "NO");
  Serial.print(" scheduler=");
  Serial.println((!completionFlag &&
                  !petRequestNeedSatisfiedThisStage() &&
                  petRequestsShown < PET_REQUESTS_PER_STAGE) ? "ENABLED" : "STOPPED");
  Serial.println("----------------------------------");
}

void printSerialHelp() {
  Serial.println();
  Serial.println("===== TANDO SERIAL TEST COMMANDS =====");
  Serial.println("p = simulate PET");
  Serial.println("1 = simulate FOOD TAG 1");
  Serial.println("2 = simulate FOOD TAG 2");
  Serial.println("s = toggle simulated SLEEP TAG present / removed");
  Serial.println("u = simulate UNKNOWN RFID reaction");
  Serial.println("l = autonomous LOOK (no progress / no LED pulse)");
  Serial.println("w = autonomous WINK (no progress / no LED pulse)");
  Serial.println("e = autonomous EYE SMILE (no progress / no LED pulse)");
  Serial.println("g = autonomous PLAY INVITE (no progress / no LED pulse)");
  Serial.println("h = preview 10 s DRUMSTICK HUNGER overlay on both displays (no Stage count)");
  Serial.println("r = preview 10 s PET REQUEST hand/affection cue (no Stage count)");
  Serial.println("b = blink");
  Serial.println("i = print demo status");
  Serial.println("t = print MPR121 PET E0/E6/E11 raw diagnostics once");
  Serial.println("c = manual MPR121 recalibration only (keep hand away)");
  Serial.println("D = RESET ALL DEMO PROGRESS / TIMER NVS");
  Serial.println("? = help");
  Serial.println("======================================");
  Serial.println();
}

void updateSerial(uint32_t now) {
  while (Serial.available()) {
    char c = Serial.read();

    switch (c) {
      case 'p':
        handlePetEvent(now);
        break;

      case '1':
        handleFoodEvent(1, now);
        break;

      case '2':
        handleFoodEvent(2, now);
        break;

      case 's':
        if (reaction == R_SLEEP && sleepTagPresent) {
          beginSleepWake(now);
        } else if (!reactionIsBusy()) {
          handleSleepEvent(now);
        }
        break;

      case 'u':
        handleUnknownRfidEvent(now);
        break;

      case 'l':
        if (!reactionIsBusy()) {
          cancelAutonomous();
          startAutonomous(AUTO_LOOK, now);
        }
        break;

      case 'w':
        if (!reactionIsBusy()) {
          cancelAutonomous();
          startAutonomous(AUTO_WINK, now);
        }
        break;

      case 'e':
        if (!reactionIsBusy()) {
          cancelAutonomous();
          startAutonomous(AUTO_SMILE, now);
        }
        break;

      case 'g':
        if (!reactionIsBusy()) {
          cancelAutonomous();
          startAutonomous(AUTO_PLAY, now);
        }
        break;

      case 'h':
        if (!hungerOverlayBlockedByPriority() && !petRequestPromptActive) {
          clearHungerPromptRuntime();
          hungerRetryPending = false;
          startHungerPrompt(now, false);
        } else {
          Serial.println("HUNGER PREVIEW BLOCKED - CARE/SLEEP/SYSTEM VISUAL ACTIVE");
        }
        break;

      case 'r':
        if (!petRequestBlockedByPriority() && !hungerPromptActive) {
          clearPetRequestPromptRuntime();
          petRequestRetryPending = false;
          startPetRequestPrompt(now, false);
        } else {
          Serial.println("PET REQUEST PREVIEW BLOCKED - CARE/REACTION VISUAL ACTIVE");
        }
        break;

      case 'b':
        startBlink(now);
        break;

      case 'i':
        printDemoStatus(now);
        break;

      case 't':
        printMprDiagnostics();
        break;

      case 'c':
        recalibrateMpr121(now);
        break;

      case 'D':
        resetDemoState(now);
        cancelAutonomous();
        clearHungerPromptRuntime();
        clearPetRequestPromptRuntime();
        chooseIdleLook(now);
        scheduleNextAutonomous(now);
        syncHungerRequestStage(now);
        syncPetRequestStage(now);
        triggerRingPulse(now);
        break;

      case '?':
        printSerialHelp();
        break;
    }
  }
}

// ============================================================
// MAIN TIMING
// ============================================================

const uint32_t FRAME_MS = 34;       // ~29 FPS; realistic for two 200x200 RGB565 SPI pushes
const uint32_t TOUCH_POLL_MS = 20;
const uint32_t RFID_POLL_MS = 80;

uint32_t lastFrame = 0;
uint32_t lastTouchPoll = 0;
uint32_t lastRfidPoll = 0;
uint32_t previousFrameTime = 0;

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  randomSeed((uint32_t)esp_random());

  // ----------------------------------------------------------
  // Canvas
  // ----------------------------------------------------------
  if (frame.getBuffer() == nullptr) {
    Serial.println("FATAL: CANVAS ALLOCATION FAILED");
    while (true) delay(1000);
  }

  // ----------------------------------------------------------
  // Display SPI
  // ----------------------------------------------------------
  pinMode(LEFT_CS, OUTPUT);
  pinMode(RIGHT_CS, OUTPUT);
  digitalWrite(LEFT_CS, HIGH);
  digitalWrite(RIGHT_CS, HIGH);

  displaySPI.begin(TFT_SCLK, -1, TFT_MOSI, -1);

  leftDisplay.begin(40000000);
  rightDisplay.begin(40000000);
  leftDisplay.setRotation(0);
  rightDisplay.setRotation(0);

  // ----------------------------------------------------------
  // Colors
  // ----------------------------------------------------------
  C_BLACK      = rgb565(0, 0, 0);
  C_WHITE      = rgb565(255, 255, 255);

  C_EYE        = rgb565(65, 235, 255);
  C_EYE_GLOW   = rgb565(0, 58, 76);
  C_EYE_SHINE  = rgb565(225, 255, 255);

  C_BLUSH      = rgb565(255, 80, 145);

  C_RING_TRACK = rgb565(7, 22, 27);
  C_RING_S1    = rgb565(85, 225, 150);   // soft green
  C_RING_S2    = rgb565(55, 220, 255);   // cyan / blue
  C_RING_S3    = rgb565(255, 195, 65);   // warm gold

  C_SLEEP_Z    = rgb565(130, 190, 255);
  C_SPARK      = rgb565(255, 235, 145);

  leftDisplay.fillScreen(C_BLACK);
  rightDisplay.fillScreen(C_BLACK);

  Serial.println("EYES READY");

  // ----------------------------------------------------------
  // Reaction LED
  // ----------------------------------------------------------
  setupReactionLed();
  Serial.println("REACTION LED READY");

  // ----------------------------------------------------------
  // MPR121
  // ----------------------------------------------------------
  Wire.begin(MPR_SDA, MPR_SCL);

  // rc.3 sensing path: final thresholds + autoconfiguration are supplied
  // directly to begin(). No experimental touched-baseline filter tuning.
  if (!mpr.begin(
        0x5A,
        &Wire,
        MPR_TOUCH_THRESHOLD,
        MPR_RELEASE_THRESHOLD,
        true)) {
    Serial.println("FATAL: MPR121 NOT FOUND");
    while (true) delay(100);
  }

  Serial.print("MPR121 READY - touch/release thresholds: ");
  Serial.print(MPR_TOUCH_THRESHOLD);
  Serial.print("/");
  Serial.print(MPR_RELEASE_THRESHOLD);
  Serial.print(" | ECR=0x");
  Serial.println(mpr.readRegister8(MPR121_ECR), HEX);

  // ----------------------------------------------------------
  // RC522
  // ----------------------------------------------------------
  SPI.begin(RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SS);
  rfid.PCD_Init();
  delay(20);
  Serial.println("RC522 READY");

  // ----------------------------------------------------------
  // NVS
  // ----------------------------------------------------------
  if (!prefs.begin("tandoDemo", false)) {
    Serial.println("FATAL: NVS / Preferences open failed");
    while (true) delay(1000);
  }

  loadPersistentState();

  uint32_t now = millis();
  lastInteractionMs = now;
  lastNvsCheckpointElapsed = savedActiveMs;

  // Validate Stage against persisted active time after an interrupted power cycle.
  if (!completionFlag) {
    if (savedActiveMs >= 2UL * STAGE_MS) {
      if (currentStage < 3) {
        currentStage = 3;
        if (visualCredits < 6) visualCredits = 6;
        stageCareMask = 0;
      }
    } else if (savedActiveMs >= STAGE_MS) {
      if (currentStage < 2) {
        currentStage = 2;
        if (visualCredits < 3) visualCredits = 3;
        stageCareMask = 0;
      }
    }
  }

  chooseIdleLook(now);
  scheduleNextBlink(now);
  scheduleNextAutonomous(now);
  syncHungerRequestStage(now);
  syncPetRequestStage(now);
  previousFrameTime = now;

  Serial.println();
  Serial.println("============================================");
  Serial.print("TANDO FIRMWARE v");
  Serial.println(TANDO_VERSION);
  Serial.println("TANDO FINAL 30-MIN DEMO READY");
  Serial.println("No cat | 2 Food Tags | Persistent Sleep Tag State");
  Serial.println("Interaction Manager + 2-of-3 Capacitive Pet + Progress Ring + NVS + LED Pulse");
  Serial.println("============================================");

  Serial.println("RFID MAP:");
  Serial.println("FOOD 1 = 96 2B CD AB");
  Serial.println("FOOD 2 = F6 33 11 AA");
  Serial.println("SLEEP  = C6 34 BD AA");

  printDemoStatus(now);
  printSerialHelp();
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  uint32_t now = millis();

  // Serial test / presentation tools.
  updateSerial(now);

  // Update active demo timer + stage gates + NVS checkpoint.
  updateDemoClock(now);

  // Complete current reaction, then service system-priority events first,
  // followed by any coalesced user reaction that arrived while busy.
  updateReaction(now);
  servicePendingSystemReaction(now);
  servicePendingUserReaction(now);

  // Inputs.
  if ((now - lastTouchPoll) >= TOUCH_POLL_MS) {
    lastTouchPoll = now;
    updateTouch(now);
  }

  if ((now - lastRfidPoll) >= RFID_POLL_MS) {
    lastRfidPoll = now;
    updateRFID(now);
  }

  servicePendingSystemReaction(now);
  servicePendingUserReaction(now);

  // Care requests run even before the Demo clock starts and while it is
  // paused for inactivity. Hunger and PET Request arbitrate so they never overlap.
  updateHungerRequestScheduler(now);
  updatePetRequestScheduler(now);

  // Generic autonomous personality remains the lowest-priority eye behavior.
  updateAutonomous(now);

  // Micro idle gaze continues between larger personality expressions.
  if (reaction == R_NONE &&
      autonomousState == AUTO_NONE &&
      (int32_t)(now - nextIdleLook) >= 0) {
    chooseIdleLook(now);
  }

  updateBlink(now);
  updateEyeTargets(now);
  updateReactionLed(now);

  // ----------------------------------------------------------
  // ~29 FPS render limit
  // ----------------------------------------------------------
  if ((now - lastFrame) < FRAME_MS) {
    return;
  }

  lastFrame = now;

  float dt = (float)(now - previousFrameTime) / 1000.0f;
  previousFrameTime = now;

  if (dt > 0.10f) dt = 0.10f;

  updateEyeDynamics(dt);

  renderDisplay(true, now);
  renderDisplay(false, now);
}
