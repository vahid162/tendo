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

// ============================================================
// TANDO - FINAL 15 MIN DEMO FIRMWARE
// Revision v6: sensitive capacitive PET session + robust 2-of-3 detection + stronger affection animation
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

// Reaction LED control only: GPIO21 drives a logic-level MOSFET gate.
// Current hardware uses a 3V / 120mA filament LED; do NOT power it directly from GPIO21.
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

uint8_t pendingUnlockStage = 0;
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

  pendingUnlockStage = 0;
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

bool reactionIsBusy() {
  return reaction != R_NONE;
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
  scheduleNextBlink(now + 3600);

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
