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
#define TANDO_VERSION_MINOR 7
#define TANDO_VERSION_PATCH 2
#define TANDO_VERSION "0.7.2-rc.4"


// ============================================================
// TANDO - FINAL 15 MIN DEMO FIRMWARE
// Firmware v0.7.2-rc.4: deterministic interaction manager + robust PET re-arm + queued reactions + stage-event fixes
// ESP32-S3 + 2x GC9A01 + MPR121 + RC522 + 1 PWM LED
//
// Demo:
//   Stage 1 : active minute 0..5
//   Stage 2 : active minute 5..10
//   Stage 3 : active minute 10..15
//   Complete: active minute 15
//
// Each stage can earn at most 3 care credits:
//   PET once + FOOD once + SLEEP once
// Extra interactions still get eye + LED feedback, but +0 progress.
// Missing visual progress is auto-filled at each 5-minute boundary so the
// presentation is guaranteed to reach 100% in 15 active demo minutes.
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

const uint32_t STAGE_MS = 5UL * 60UL * 1000UL;
const uint32_t TOTAL_DEMO_MS = 15UL * 60UL * 1000UL;

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

const uint32_t NVS_STATE_VERSION = 3;

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
    return;
  }

  demoStarted = prefs.getBool("started", false);
  completionFlag = prefs.getBool("done", false);
  savedActiveMs = prefs.getUInt("elapsed", 0);
  currentStage = prefs.getUChar("stage", 1);
  visualCredits = prefs.getUChar("credits", 0);
  stageCareMask = prefs.getUChar("mask", 0);

  if (currentStage < 1 || currentStage > 3) currentStage = 1;
  if (visualCredits > TOTAL_VISUAL_CREDITS) visualCredits = TOTAL_VISUAL_CREDITS;
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
    minDelay = 700;
    maxDelay = 1600;
  } else if (currentStage == 3) {
    minDelay = 550;
    maxDelay = 1350;
  }

  nextIdleLook = now + random(minDelay, maxDelay);
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

  notifyUserInteraction(now);
  triggerLedPulse(LED_REACTION_PEAK, 1, now);
  registerCareCredit(CARE_PET_BIT, "PET", now);

  if (reaction == R_NONE || reaction == R_PET) {
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

  notifyUserInteraction(now);
  triggerLedPulse(LED_REACTION_PEAK, 1, now);

  if (foodNumber == 1) {
    Serial.println("FOOD TAG 1 ACCEPTED");
  } else {
    Serial.println("FOOD TAG 2 ACCEPTED");
  }

  registerCareCredit(CARE_FOOD_BIT, "FOOD", now);

  if (!reactionIsBusy()) {
    startFoodReaction(now);
  } else {
    // Both food tags use the same visual, but keep the latest tag number for logs.
    pendingFoodVisual = foodNumber;
  }
}

void handleSleepEvent(uint32_t now) {
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
    reaction = R_NONE;
    foodBlinkTriggered = false;
    startSleepReaction(now);
  }
}

void handleUnknownRfidEvent(uint32_t now) {
  if (reaction == R_SLEEP || pendingSleepVisual) return;

  notifyUserInteraction(now);
  triggerLedPulse(LED_REACTION_PEAK, 1, now);

  if (!reactionIsBusy()) {
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

void servicePendingSystemReaction(uint32_t now) {
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

void finishReaction() {
  if (reaction == R_SLEEP) {
    sleepTagPresent = false;
    sleepWakeStart = 0;
  }

  reaction = R_NONE;
  foodBlinkTriggered = false;
}

void updateReaction(uint32_t now) {
  if (reaction == R_NONE) {
      return;
  }

  uint32_t elapsed = now - reactionStart;

  switch (reaction) {
    case R_PET:
      // Long enough to be unmistakable, but still quick enough for repeated play.
      if (elapsed >= 4800) finishReaction();
      break;

    case R_FOOD:
      if (!foodBlinkTriggered && elapsed >= 450) {
        startBlink(now);
        foodBlinkTriggered = true;
      }
      if (elapsed >= 3350) finishReaction();
      break;

    case R_SLEEP:
      // Persistent state: never auto-finish while the SLEEP tag is present.
      if (!sleepTagPresent) {
        if (sleepWakeStart == 0) {
          sleepWakeStart = now;
        }

        if ((now - sleepWakeStart) >= SLEEP_WAKE_MS) {
          finishReaction();
          chooseIdleLook(now);
        }
      }
      break;

    case R_CONFUSED:
      if (elapsed >= 950) finishReaction();
      break;

    case R_UNLOCK2:
      if (elapsed >= 1800) finishReaction();
      break;

    case R_UNLOCK3:
      if (elapsed >= 2200) finishReaction();
      break;

    case R_COMPLETE:
      if (elapsed >= 4300) finishReaction();
      break;

    default:
      finishReaction();
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

      if (reaction == R_NONE && (int32_t)(now - nextBlink) >= 0) {
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
    // Tiny Stage 3 personality drift, without changing identity.
    if (currentStage == 3) {
      float tiny = sinf(now * 0.0017f) * 0.7f;
      leftEye.targetY += tiny;
      rightEye.targetY += tiny;
      specialGlowTarget = 0.16f;
    } else if (currentStage == 2) {
      specialGlowTarget = 0.08f;
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
      float bounce = sinf((elapsed - 500) * 0.020f) * 2.0f;
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
    float shake = sinf(elapsed * 0.055f) * 10.0f;
    leftEye.targetX = shake;
    rightEye.targetX = -shake;
    leftEye.targetY = 0.0f;
    rightEye.targetY = 0.0f;
    surpriseTarget = 0.35f;
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
  if (completionFlag) return C_RING_S3;
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

  float closeAmount = blinkAmount;

  if (sleepClose > blinkAmount && sleepClose > 0.001f) {
    // Sleep closure is top-lid dominant. Keep the lower edge almost fixed
    // while the upper lid settles downward. This reads more naturally than
    // shrinking the eye equally from top and bottom.
    float originalH = h;
    h *= (1.0f - sleepClose * 0.93f);
    if (h < 4.0f) h = 4.0f;
    cy += (originalH - h) * 0.46f;
    closeAmount = sleepClose;
  } else {
    h *= (1.0f - blinkAmount * 0.93f);
    if (h < 4.0f) h = 4.0f;
  }

  // PET has its own slow, shallow eyelid soften. It is intentionally much
  // gentler than a normal blink: the eyes stay open, but briefly "melt" into
  // the affectionate pose, which makes the reaction more noticeable.
  if (reaction == R_PET && sleepClose < 0.01f && blinkAmount < 0.01f) {
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

  drawFantasyEye(leftSide, now);
  drawSleepGraphics(leftSide, now);
  drawCompletionSparkles(leftSide, now);
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
// Rules:
//   - NO long-touch / heart mode.
//   - One electrode by itself never triggers PET.
//   - Any 2 different electrodes are valid: E0+E1, E0+E2, E1+E2.
//   - All 3 are valid too.
//   - Order is irrelevant.
//   - Direct electrical contact with the electrode is NOT required.
//     The intended use is capacitive sensing through the enclosure / cover.
//   - The user needs >= 1.0 second of ACTUAL capacitive presence in one
//     petting session. Short air gaps while moving between pads are tolerated.
//   - After a PET, all E0/E1/E2 must be released stably before re-arm.
//
// Why this is more reliable than v5:
//   v5 measured 2 seconds from the first contact and reset the whole gesture
//   after only 350 ms with no electrode active. With separated physical pads,
//   normal stroking could easily exceed that gap and silently reset.
//   v6 accumulates actual touch time and allows a much larger movement gap.
// ============================================================

const uint16_t PET_E0_MASK = 0x01;
const uint16_t PET_E1_MASK = 0x02;
const uint16_t PET_E2_MASK = 0x04;
const uint16_t PET_ALL_MASK = PET_E0_MASK | PET_E1_MASK | PET_E2_MASK;

// MPR121 sensitivity. Lower values = more sensitive.
// Adafruit defaults are 12/6. 6/3 is deliberately more sensitive for the
// larger/remote pads used on Tando. If the real enclosure becomes noisy,
// raise these to 8/4 before changing the gesture logic.
const uint8_t MPR_TOUCH_THRESHOLD = 6;
const uint8_t MPR_RELEASE_THRESHOLD = 3;

// Tando uses only MPR121 electrodes E0/E1/E2. The Adafruit library starts all
// 12 electrodes by default, so startup is explicitly reconfigured to run only
// the three physical PET electrodes after the rest of the hardware has settled.
const uint8_t MPR_ACTIVE_ELECTRODES = 3;
const uint8_t MPR_ECR_RUN_3 = 0x80 | MPR_ACTIVE_ELECTRODES;
const uint32_t MPR_STARTUP_SETTLE_MS = 800;
const uint32_t MPR_POST_START_SETTLE_MS = 250;

const uint32_t PET_MIN_ACTIVE_MS = 1000;         // actual accumulated capacitive presence time
const uint32_t PET_ELECTRODE_CONFIRM_MS = 20;   // one additional stable sample
const uint32_t PET_SESSION_GAP_MS = 1200;        // finger travel between pads
const uint32_t PET_SECOND_PAD_WINDOW_MS = 3000;  // reject a stale single-pad hold
const uint32_t PET_SESSION_MAX_MS = 6000;
const uint32_t PET_REARM_CLEAR_MS = 220;
const uint32_t PET_EVENT_REFRACTORY_MS = 800;

bool petGestureActive = false;
bool petGestureLocked = false;
bool petRequireFullRelease = false;
uint32_t petSessionStartMs = 0;
uint32_t petFirstQualifiedAt = 0;
uint32_t petActiveAccumMs = 0;
uint32_t petLastUpdateMs = 0;
uint32_t petLastAnyTouchMs = 0;
uint32_t petClearSince = 0;
uint32_t lastPetTriggerAt = 0;
bool hasPetTrigger = false;

// Electrodes left active by MPR121 after a completed PET are not allowed to
// START the next session until they physically release. They may still act as
// the companion zone after a genuinely new electrode starts a fresh gesture.
uint16_t petStartBlockedMask = 0;

uint32_t petElectrodeOnSince[3] = {0, 0, 0};
uint16_t petQualifiedMask = 0;

uint8_t countPetBits(uint16_t mask) {
  uint8_t count = 0;
  if (mask & PET_E0_MASK) count++;
  if (mask & PET_E1_MASK) count++;
  if (mask & PET_E2_MASK) count++;
  return count;
}

void clearPetCandidate() {
  petGestureActive = false;
  petSessionStartMs = 0;
  petFirstQualifiedAt = 0;
  petActiveAccumMs = 0;
  petLastUpdateMs = 0;
  petLastAnyTouchMs = 0;
  petQualifiedMask = 0;
  petElectrodeOnSince[0] = 0;
  petElectrodeOnSince[1] = 0;
  petElectrodeOnSince[2] = 0;
}

void lockPetUntilClear(uint32_t now) {
  clearPetCandidate();
  petGestureLocked = true;
  petRequireFullRelease = false;
  petClearSince = 0;
  lastPetTriggerAt = now;
  hasPetTrigger = true;
}

void expirePetSessionUntilRelease() {
  clearPetCandidate();
  petGestureLocked = false;
  petRequireFullRelease = true;
  petClearSince = 0;
}

void printPetMask(uint16_t mask, uint32_t activeMs) {
  Serial.print("PET CAPACITIVE: ");

  bool first = true;
  if (mask & PET_E0_MASK) {
    Serial.print("E0");
    first = false;
  }
  if (mask & PET_E1_MASK) {
    if (!first) Serial.print("+");
    Serial.print("E1");
    first = false;
  }
  if (mask & PET_E2_MASK) {
    if (!first) Serial.print("+");
    Serial.print("E2");
  }

  Serial.print(" | active=");
  Serial.print(activeMs);
  Serial.println(" ms -> PET");
}

// One-shot diagnostic for real hardware calibration.
// Baseline - filtered is the useful capacitive delta: larger positive values
// mean a stronger touch. The MPR121 itself also exposes the final touch bits.
void printMprDiagnostics() {
  uint16_t hwTouched = mpr.touched() & PET_ALL_MASK;
  Serial.println("---- MPR121 E0/E1/E2 ----");
  for (uint8_t i = 0; i < 3; i++) {
    uint16_t baseline = mpr.baselineData(i);
    uint16_t filtered = mpr.filteredData(i);
    int16_t delta = (int16_t)baseline - (int16_t)filtered;

    Serial.print("E");
    Serial.print(i);
    Serial.print(" baseline=");
    Serial.print(baseline);
    Serial.print(" filtered=");
    Serial.print(filtered);
    Serial.print(" delta=");
    Serial.print(delta);
    Serial.print(" touched=");
    Serial.println((hwTouched & (1U << i)) ? "YES" : "NO");
  }
  Serial.print("thresholds touch/release = ");
  Serial.print(MPR_TOUCH_THRESHOLD);
  Serial.print("/");
  Serial.println(MPR_RELEASE_THRESHOLD);
  Serial.print("MPR121 ECR=0x");
  Serial.print(mpr.readRegister8(MPR121_ECR), HEX);
  Serial.print(" activeElectrodes=");
  Serial.println(MPR_ACTIVE_ELECTRODES);
  Serial.print("pet state: locked=");
  Serial.print(petGestureLocked ? "YES" : "NO");
  Serial.print(" fullRelease=");
  Serial.print(petRequireFullRelease ? "YES" : "NO");
  Serial.print(" startBlockedMask=0x");
  Serial.println(petStartBlockedMask, HEX);
  Serial.println("--------------------------");
}

void updateTouch(uint32_t now) {
  uint16_t rawTouched = mpr.touched() & PET_ALL_MASK;

  // A residual electrode is blocked only while the MPR121 still reports it as
  // active. The moment it really releases, it automatically becomes eligible
  // to start a future PET again.
  petStartBlockedMask &= rawTouched;
  uint16_t freshTouched = rawTouched & ~petStartBlockedMask;

  // PET never runs during the persistent SLEEP state. Any electrode that is
  // already active while sleeping must release once before it can initiate a
  // PET after wake-up.
  if (reaction == R_SLEEP || pendingSleepVisual) {
    petStartBlockedMask |= rawTouched;
    clearPetCandidate();
    petGestureLocked = false;
    petRequireFullRelease = false;
    petClearSince = 0;
    return;
  }

  // A stale one-pad session must release all electrodes that were genuinely
  // new in that session before another session can begin. Old residual pads
  // are excluded here so one stuck capacitive channel cannot deadlock PET.
  if (petRequireFullRelease) {
    if (freshTouched == 0) {
      if (petClearSince == 0) petClearSince = now;
      if ((now - petClearSince) >= PET_REARM_CLEAR_MS) {
        petRequireFullRelease = false;
        petClearSince = 0;
      }
    } else {
      petClearSince = 0;
    }
    return;
  }

  // After a successful PET, tolerate one electrode that remains capacitively
  // active. Once the reaction is re-armed, remember that residual electrode
  // as start-blocked so it cannot immediately create a phantom new session.
  if (petGestureLocked) {
    if (countPetBits(rawTouched) <= 1) {
      if (petClearSince == 0) petClearSince = now;

      if ((now - petClearSince) >= PET_REARM_CLEAR_MS &&
          (!hasPetTrigger || (now - lastPetTriggerAt) >= PET_EVENT_REFRACTORY_MS)) {
        petStartBlockedMask |= rawTouched;
        petGestureLocked = false;
        petClearSince = 0;
      }
    } else {
      petClearSince = 0;
    }
    return;
  }

  // A residual/stuck electrode is never enough to START a new PET. We require
  // at least one genuinely new electrode transition first.
  if (!petGestureActive) {
    if (freshTouched == 0) return;

    petGestureActive = true;
    petSessionStartMs = now;
    petFirstQualifiedAt = 0;
    petActiveAccumMs = 0;
    petLastUpdateMs = now;
    petLastAnyTouchMs = now;
    petQualifiedMask = 0;
    petElectrodeOnSince[0] = 0;
    petElectrodeOnSince[1] = 0;
    petElectrodeOnSince[2] = 0;
  }

  if ((now - petSessionStartMs) > PET_SESSION_MAX_MS) {
    expirePetSessionUntilRelease();
    return;
  }

  // Protect accumulation from an unexpectedly long loop stall.
  uint32_t dt = now - petLastUpdateMs;
  if (dt > 100) dt = 100;
  petLastUpdateMs = now;

  // Count time only while at least one non-residual electrode is genuinely
  // active. A stuck old pad can support a new gesture, but cannot accumulate
  // the 1-second PET time by itself.
  if (freshTouched != 0) {
    petActiveAccumMs += dt;
    petLastAnyTouchMs = now;
  } else {
    // Give the finger time to travel between physically separated pads.
    if ((now - petLastAnyTouchMs) > PET_SESSION_GAP_MS) {
      clearPetCandidate();
      return;
    }
  }

  // ----------------------------------------------------------
  // Confirm and remember every distinct electrode present in this session.
  // A residual pad may count as the companion zone only AFTER a fresh pad has
  // started the session. This preserves repeated E0+E1 petting even if E0
  // remains capacitively active for a while after the previous PET.
  // ----------------------------------------------------------
  const uint16_t masks[3] = { PET_E0_MASK, PET_E1_MASK, PET_E2_MASK };

  for (uint8_t i = 0; i < 3; i++) {
    if (rawTouched & masks[i]) {
      if (petElectrodeOnSince[i] == 0) petElectrodeOnSince[i] = now;

      if ((now - petElectrodeOnSince[i]) >= PET_ELECTRODE_CONFIRM_MS) {
        bool wasQualified = (petQualifiedMask & masks[i]) != 0;
        petQualifiedMask |= masks[i];

        if (!wasQualified && petFirstQualifiedAt == 0) {
          petFirstQualifiedAt = now;
        }
      }
    } else {
      petElectrodeOnSince[i] = 0;
    }
  }

  // A second distinct pad must appear soon enough. This rejects a long
  // stationary hold that later happens to brush another electrode.
  if (countPetBits(petQualifiedMask) < 2 &&
      petFirstQualifiedAt != 0 &&
      (now - petFirstQualifiedAt) > PET_SECOND_PAD_WINDOW_MS) {
    expirePetSessionUntilRelease();
    return;
  }

  // Trigger when 2-of-3 zones have been visited and at least one fresh
  // capacitive electrode has contributed >= 1 s of actual presence time.
  if (countPetBits(petQualifiedMask) >= 2 &&
      petActiveAccumMs >= PET_MIN_ACTIVE_MS) {

    uint16_t acceptedMask = petQualifiedMask;
    uint32_t acceptedActiveMs = petActiveAccumMs;

    printPetMask(acceptedMask, acceptedActiveMs);
    handlePetEvent(now);
    lockPetUntilClear(now);
  }
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
  Serial.println("b = blink");
  Serial.println("i = print demo status");
  Serial.println("t = print MPR121 E0/E1/E2 raw diagnostics once");
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

      case 'b':
        startBlink(now);
        break;

      case 'i':
        printDemoStatus(now);
        break;

      case 't':
        printMprDiagnostics();
        break;

      case 'D':
        resetDemoState(now);
        chooseIdleLook(now);
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

  // ----------------------------------------------------------
  // MPR121 - initialize LAST, after the rest of the board has settled.
  // ----------------------------------------------------------
  Serial.println("MPR121 STARTUP SETTLE...");
  delay(MPR_STARTUP_SETTLE_MS);
  Wire.begin(MPR_SDA, MPR_SCL);

  // begin() is used first with autoconfig disabled. The Adafruit library
  // briefly enables all 12 electrodes at the end of begin(), so immediately
  // return the controller to Stop Mode before applying Tando's final run state.
  if (!mpr.begin(
        0x5A,
        &Wire,
        MPR_TOUCH_THRESHOLD,
        MPR_RELEASE_THRESHOLD,
        false)) {
    Serial.println("FATAL: MPR121 NOT FOUND");
    while (true) delay(100);
  }

  mpr.writeRegister(MPR121_ECR, 0x00);  // Stop Mode
  delay(2);

  // Apply the final sensing configuration while stopped. Autoconfiguration is
  // enabled only for the final Stop -> Run transition below.
  mpr.setThresholds(MPR_TOUCH_THRESHOLD, MPR_RELEASE_THRESHOLD);
  mpr.setAutoconfig(true);
  delay(2);

  // Run ONLY E0/E1/E2. 0x80 keeps the same calibration-lock mode used by the
  // Adafruit default ECR setting; the low nibble selects three electrodes.
  mpr.writeRegister(MPR121_ECR, MPR_ECR_RUN_3);
  delay(MPR_POST_START_SETTLE_MS);

  // Sensitivity remains intentionally higher than Adafruit's default 12/6.
  // The two-of-three + >=1 s gesture rule provides software protection against
  // accidental single-pad noise. Direct contact is not required; the electrodes
  // are intended to sense the hand capacitively through the product enclosure.
  Serial.print("MPR121 READY - touch/release thresholds: ");
  Serial.print(MPR_TOUCH_THRESHOLD);
  Serial.print("/");
  Serial.print(MPR_RELEASE_THRESHOLD);
  Serial.print(" | ECR=0x");
  Serial.print(mpr.readRegister8(MPR121_ECR), HEX);
  Serial.println(" | active electrodes: E0/E1/E2");

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
  previousFrameTime = now;

  Serial.println();
  Serial.println("============================================");
  Serial.print("TANDO FIRMWARE v");
  Serial.println(TANDO_VERSION);
  Serial.println("TANDO FINAL 15-MIN DEMO READY");
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

  // Autonomous idle eye movement only when no reaction is active.
  if (reaction == R_NONE && (int32_t)(now - nextIdleLook) >= 0) {
    chooseIdleLook(now);
  }

  updateBlink(now);
  updateEyeTargets(now);
  updateReactionLed(now);

  // ----------------------------------------------------------
  // 40 FPS render limit
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
