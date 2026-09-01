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
