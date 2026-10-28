#include "Eyes.h"
#include "TandoConfig.h"
#if TANDO_WOKWI_MODE
#include "../../simulator/wokwi/tracked_canvas.h"
#endif
#include "Demo.h"
#include "Reactions.h"
#include "CareRequests.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <math.h>

// Keeps display construction, the shared canvas, rendering, and eye-only motion together.

// Local declarations replace Arduino-generated prototypes.
static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);
static uint16_t blend565(uint16_t a, uint16_t b, float t);
static float clamp01(float v);
static float smoothFollow(float current, float target, float dt, float speed);
static float mapF(float value, float inMin, float inMax, float outMin, float outMax);
static uint32_t chooseAutonomousDelayMs();
static void rememberAutonomous(uint8_t state);
static int applyAutonomousHistoryPenalty(uint8_t state, int weight);
static uint8_t chooseAutonomousState(uint32_t now);
static uint32_t chooseAutonomousDurationMs(uint8_t state);
static float autonomousLidAmount(bool leftSide, uint32_t now);
static float petRequestLidAmount(uint32_t now);
static float sleepRequestLidAmount(uint32_t now);
static void applyAutonomousEyeTargets(uint32_t now);
static void applyPetRequestEyeCue(uint32_t now);
static void applySleepRequestEyeCue(uint32_t now);
static void drawZSymbol(int x, int y, int s, uint16_t color);
static void drawSleepGraphics(bool leftSide, uint32_t now);
static void drawSleepRequestGraphics(bool leftSide, uint32_t now);
static uint16_t getRingColor();
static float getRingPulseAmount(uint32_t now);
static void drawProgressRing(uint32_t now);
static void drawCompletionSparkles(bool leftSide, uint32_t now);
static void drawChickenDrumstick(int cx, int cy, float phase);
static void drawHungerFoodOverlay(bool leftSide, uint32_t now);
static void drawPetRequestHeart(int cx, int cy, int size, uint16_t color);
static void drawPetRequestParen(int cx, int cy, int span, bool leftSide, uint16_t color);
static void drawPetRequestOverlay(bool leftSide, uint32_t now);
static void drawFantasyEye(bool leftSide, uint32_t now);
static void pushLeft();
static void pushRight();

static SPIClass displaySPI(HSPI);
static Adafruit_GC9A01A leftDisplay(
  &displaySPI,
  LEFT_DC,
  LEFT_CS,
  LEFT_RST
);
static Adafruit_GC9A01A rightDisplay(
  &displaySPI,
  RIGHT_DC,
  RIGHT_CS,
  RIGHT_RST
);
#if TANDO_WOKWI_MODE
static TandoTrackedCanvas frame(FRAME_W, FRAME_H);
static_assert(FRAME_W % 2 == 0, "SPI row stride must preserve 32-bit alignment");
static_assert(FRAME_H % 2 == 0, "Background storage uses two equal row blocks");
uint16_t *leftFrameCache = nullptr;
uint16_t *rightFrameCache = nullptr;
bool leftFrameCacheValid = false;
bool rightFrameCacheValid = false;
uint16_t *renderBackground[2] = {nullptr, nullptr};
bool renderBackgroundValid = false;
TandoRingBackgroundKey renderBackgroundKey = {};
TandoFrameRegion lastCanvasDynamic = {0, 0, 0, 0};
TandoFrameRegion renderDiffRegion = {0, 0, FRAME_W, FRAME_H};
TandoEyeRenderState eyeRenderState[2];
uint32_t simulatorFrameIntervalMs = FRAME_MS;
static bool simulatorUseBands = true;
#else
static GFXcanvas16 frame(FRAME_W, FRAME_H);
#endif

#if TANDO_WOKWI_MODE // TANDO_PERF_PROBE
uint32_t simProbePairUs = 0;
uint32_t simProbeDrawUs = 0;
uint32_t simProbeSpiUs = 0;
uint32_t simProbeFrames = 0;
uint32_t simProbeWaits = 0;
uint32_t simProbeLastLoop = 0;
uint32_t simProbeMaxGap = 0;
uint32_t simProbeTransferBytes = 0;
uint32_t simProbeLastReportMs = 0;

void printSimProbe() {
  Serial.printf("SIMPERF uptime_ms=%lu frames=%lu waits=%lu pair_us=%lu draw_us=%lu spi_us=%lu max_loop_ms=%lu bytes=%lu cache_l=%d cache_r=%d bg_cache=%d heap=%lu target_ms=%lu bands=%d\r\n",
                (unsigned long)millis(), (unsigned long)simProbeFrames,
                (unsigned long)simProbeWaits, (unsigned long)simProbePairUs,
                (unsigned long)simProbeDrawUs, (unsigned long)simProbeSpiUs,
                (unsigned long)simProbeMaxGap, (unsigned long)simProbeTransferBytes,
                leftFrameCache != nullptr, rightFrameCache != nullptr,
                renderBackground[0] != nullptr && renderBackground[1] != nullptr,
                (unsigned long)ESP.getFreeHeap(), (unsigned long)simulatorFrameIntervalMs,
                simulatorUseBands);
}
#endif // TANDO_PERF_PROBE

#if TANDO_WOKWI_MODE
static uint32_t simProbeFrameStart;

void toggleSimulatorFrameInterval() {
  simulatorFrameIntervalMs = simulatorFrameIntervalMs == FRAME_MS ? 120 : FRAME_MS;
  Serial.printf("SIM: target_ms=%lu\r\n", (unsigned long)simulatorFrameIntervalMs);
}

void toggleSimulatorTransferMode() {
  simulatorUseBands = !simulatorUseBands;
  Serial.printf("SIM: transfer=%s\r\n", simulatorUseBands ? "BANDS" : "BBOX");
}

void noteSimulatorLoop(uint32_t now) {
  if (simProbeLastLoop && now - simProbeLastLoop > simProbeMaxGap) {
    simProbeMaxGap = now - simProbeLastLoop;
  }
  simProbeLastLoop = now;
}

void noteSimulatorWait() { simProbeWaits++; }

void beginSimulatorFrame() {
  simProbeFrameStart = micros();
  simProbeSpiUs = 0;
  simProbeDrawUs = 0;
  simProbeTransferBytes = 0;
}

void endSimulatorFrame() {
  simProbePairUs = micros() - simProbeFrameStart;
  simProbeFrames++;
  uint32_t now = millis();
  if (simProbeFrames == 3 || simProbeFrames == 10 || now - simProbeLastReportMs >= 5000) {
    simProbeLastReportMs = now;
    printSimProbe();
  }
}

void setupSimulatorCaches() {
  leftFrameCache = (uint16_t *)malloc(FRAME_W * FRAME_H * sizeof(uint16_t));
  rightFrameCache = (uint16_t *)malloc(FRAME_W * FRAME_H * sizeof(uint16_t));
  Serial.printf("SIM: frame caches L=%s R=%s (OFF uses full frames)\r\n",
                leftFrameCache ? "ON" : "OFF", rightFrameCache ? "ON" : "OFF");
  const size_t backgroundBlockBytes = FRAME_W * (FRAME_H / 2) * sizeof(uint16_t);
  renderBackground[0] = (uint16_t *)malloc(backgroundBlockBytes);
  renderBackground[1] = (uint16_t *)malloc(backgroundBlockBytes);
  if (!renderBackground[0] || !renderBackground[1]) {
    free(renderBackground[0]);
    free(renderBackground[1]);
    renderBackground[0] = renderBackground[1] = nullptr;
  }
  Serial.printf("SIM: render background=%s (OFF uses full clear/scan)\r\n",
                renderBackground[0] && renderBackground[1] ? "ON" : "OFF");
}
#endif

static uint16_t C_BLACK;
static uint16_t C_WHITE;
static uint16_t C_EYE;
static uint16_t C_EYE_GLOW;
static uint16_t C_EYE_SHINE;
static uint16_t C_BLUSH;
static uint16_t C_RING_TRACK;
static uint16_t C_RING_S1;
static uint16_t C_RING_S2;
static uint16_t C_RING_S3;
static uint16_t C_SLEEP_Z;
static uint16_t C_SPARK;

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

static uint32_t ringPulseStart = 0;

void triggerRingPulse(uint32_t now) {
  ringPulseStart = now;
}

static EyeMotion leftEye  = {0, 0, 0, 0};
static EyeMotion rightEye = {0, 0, 0, 0};
static float idleX = 0.0f;
static float idleY = 0.0f;
uint32_t nextIdleLook = 0;
static float happyBlend = 0.0f;
static float happyTarget = 0.0f;
static float surpriseBlend = 0.0f;
static float surpriseTarget = 0.0f;
static float sleepClose = 0.0f;
static float sleepCloseTarget = 0.0f;
static float specialGlow = 0.0f;
static float specialGlowTarget = 0.0f;
BlinkState blinkState = BLINK_OPEN;
float blinkAmount = 0.0f;
uint32_t blinkStart = 0;
uint32_t nextBlink = 0;

void scheduleNextBlink(uint32_t now) {
  nextBlink = now + random(2600, 5600);
}

void startBlink(uint32_t now) {
  if (blinkState != BLINK_OPEN) return;
  blinkState = BLINK_CLOSING;
  blinkStart = now;
}

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

AutonomousState autonomousState = AUTO_NONE;
static uint32_t autonomousStart = 0;
static uint32_t autonomousDuration = 0;
static uint32_t nextAutonomousAt = 0;
static uint8_t autonomousVariant = 0;
static bool autonomousWinkLeft = true;
static AutonomousState recentAutonomous[3] = {AUTO_NONE, AUTO_NONE, AUTO_NONE};

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
  if (reaction != R_NONE || sleepRequestPending) return;
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

void updateAutonomous(uint32_t now) {
  if (reaction != R_NONE || sleepRequestPending) {
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

float sleepRequestLidAmount(uint32_t now) {
  if (!sleepRequestPending || reaction != R_NONE) return 0.0f;

  // 35-55% drowsy upper-lid closure: distinct from both a normal blink and
  // the fully closed persistent R_SLEEP state.
  float slowMotion = sinf((float)now * 0.00135f) * 0.05f;
  return 0.45f + slowMotion;
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

void applySleepRequestEyeCue(uint32_t now) {
  if (!sleepRequestPending || reaction != R_NONE) return;

  // Calm low/center gaze with a deliberately slow drift. The eyes remain
  // visible; sleepRequestLidAmount() supplies the heavy upper eyelids.
  float slowSway = sinf((float)now * 0.00135f) * 0.75f;
  float slowBob = sinf((float)now * 0.00100f) * 0.45f;

  leftEye.targetX = slowSway;
  rightEye.targetX = slowSway;
  leftEye.targetY = 5.0f + slowBob;
  rightEye.targetY = 5.0f + slowBob;
  specialGlowTarget = 0.05f;
}

void updateBlink(uint32_t now) {
  switch (blinkState) {
    case BLINK_OPEN:
      blinkAmount = 0.0f;

      if (reaction == R_NONE &&
          autonomousState == AUTO_NONE &&
          !sleepRequestPending &&
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
    // Sleep Request is higher than both Care overlays and generic autonomous
    // personality, but it deliberately remains below a real reaction.
    if (sleepRequestPending) {
      applySleepRequestEyeCue(now);
      return;
    }

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

void drawSleepRequestGraphics(bool leftSide, uint32_t now) {
  if (!sleepRequestPending || reaction != R_NONE) return;

  // A subtle pair of floating Z cues supports the heavy-but-open eyelids.
  // Unlike drawSleepGraphics(), this runs on both eyes and never implies the
  // fully closed persistent R_SLEEP state.
  float t = (float)now / 1000.0f + (leftSide ? 0.0f : 0.18f);
  int drift = (int)((sinf(t * 1.35f) + 1.0f) * 2.0f);
  uint16_t c = blend565(C_SLEEP_Z, C_WHITE, 0.06f + 0.10f * (0.5f + 0.5f * sinf(t * 1.35f)));

  drawZSymbol(158, 57 - drift, 6, c);
  drawZSymbol(170, 44 - drift, 4, blend565(c, C_BLACK, 0.20f));
}

#if TANDO_WOKWI_MODE
struct ProgressRingSegment {
  uint8_t x1;
  uint8_t y1;
  uint8_t x2;
  uint8_t y2;
};

static ProgressRingSegment progressRingGeometry[120][4];

// Conservative support for every cached ring segment and all four thicknesses.
// Each half-row stores [first, end), including any Bresenham gaps inside it.
struct ProgressRingRowSpan {
  uint8_t first;
  uint8_t end;
};
static ProgressRingRowSpan progressRingRowSpans[FRAME_H][2];

void initProgressRingGeometry() {
  for (uint16_t y = 0; y < FRAME_H; y++) {
    for (uint8_t half = 0; half < 2; half++) {
      progressRingRowSpans[y][half] = {FRAME_W, 0};
    }
  }
  const int segments = 120;      // 3 degrees per segment
  const float segmentDeg = 360.0f / (float)segments;
  const float radius = 96.0f;
  const float centerX = 100.0f;
  const float centerY = 100.0f;

  // Geometry is constant; calculate it once with the target's original math.
  for (int i = 0; i < segments; i++) {
    float startDeg = -90.0f + i * segmentDeg;
    float endDeg = startDeg + segmentDeg;
    float a1 = startDeg * DEG_TO_RAD;
    float a2 = endDeg * DEG_TO_RAD;

    for (int t = 0; t < 4; t++) {
      float r = radius - (float)t;
      ProgressRingSegment &segment = progressRingGeometry[i][t];
      segment.x1 = (int)(centerX + cosf(a1) * r);
      segment.y1 = (int)(centerY + sinf(a1) * r);
      segment.x2 = (int)(centerX + cosf(a2) * r);
      segment.y2 = (int)(centerY + sinf(a2) * r);

      // A line stays within its endpoint bounds; retain both half-row spans.
      int x1 = segment.x1 < segment.x2 ? segment.x1 : segment.x2;
      int x2 = (segment.x1 > segment.x2 ? segment.x1 : segment.x2) + 1;
      int y1 = segment.y1 < segment.y2 ? segment.y1 : segment.y2;
      int y2 = segment.y1 > segment.y2 ? segment.y1 : segment.y2;
      if (y2 >= FRAME_H) y2 = FRAME_H - 1;
      for (int y = y1; y <= y2; y++) {
        for (uint8_t half = 0; half < 2; half++) {
          int halfFirst = half ? FRAME_W / 2 : 0;
          int halfEnd = half ? FRAME_W : FRAME_W / 2;
          int first = x1 > halfFirst ? x1 : halfFirst;
          int end = x2 < halfEnd ? x2 : halfEnd;
          if (first < end) {
            ProgressRingRowSpan &span = progressRingRowSpans[y][half];
            if (first < span.first) span.first = first;
            if (end > span.end) span.end = end;
          }
        }
      }
    }
  }
}

#endif

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
#if !TANDO_WOKWI_MODE
  const float radius = 96.0f;
  const float centerX = 100.0f;
  const float centerY = 100.0f;
#endif

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
#if !TANDO_WOKWI_MODE
    float endDeg = startDeg + segmentDeg;
#endif
    float midDeg = startDeg + segmentDeg * 0.5f;

#if TANDO_WOKWI_MODE
    const ProgressRingSegment &track = progressRingGeometry[i][0];
    frame.drawLine(track.x1, track.y1, track.x2, track.y2, C_RING_TRACK);
#else
    float a1 = startDeg * DEG_TO_RAD;
    float a2 = endDeg * DEG_TO_RAD;

    // Thin dark track.
    int tx1 = (int)(centerX + cosf(a1) * radius);
    int ty1 = (int)(centerY + sinf(a1) * radius);
    int tx2 = (int)(centerX + cosf(a2) * radius);
    int ty2 = (int)(centerY + sinf(a2) * radius);
    frame.drawLine(tx1, ty1, tx2, ty2, C_RING_TRACK);
#endif

    // Angular distance from the 12 o'clock origin, normalized to -180..+180.
    float delta = midDeg - (-90.0f);
    while (delta > 180.0f) delta -= 360.0f;
    while (delta < -180.0f) delta += 360.0f;

    if (fabsf(delta) <= halfSpanDeg) {
      for (int t = 0; t < thickness; t++) {
#if TANDO_WOKWI_MODE
        const ProgressRingSegment &segment = progressRingGeometry[i][t];
        frame.drawLine(segment.x1, segment.y1, segment.x2, segment.y2, ringColor);
#else
        float r = radius - (float)t;
        int x1 = (int)(centerX + cosf(a1) * r);
        int y1 = (int)(centerY + sinf(a1) * r);
        int x2 = (int)(centerX + cosf(a2) * r);
        int y2 = (int)(centerY + sinf(a2) * r);
        frame.drawLine(x1, y1, x2, y2, ringColor);
#endif
      }
    }
  }
}

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
  if (!hungerPromptActive || reaction != R_NONE || sleepRequestPending) return;

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

void drawPetRequestHeart(int cx, int cy, int size, uint16_t color) {
  int r = size / 2;
  if (r < 3) r = 3;

  frame.fillCircle(cx - r, cy - r / 2, r, color);
  frame.fillCircle(cx + r, cy - r / 2, r, color);
  frame.fillTriangle(
    cx - size,
    cy - r / 2,
    cx + size,
    cy - r / 2,
    cx,
    cy + size + 3,
    color
  );
}

void drawPetRequestParen(int cx, int cy, int span, bool leftSide, uint16_t color) {
  int dir = leftSide ? -1 : 1;
  int x0 = cx + dir * span;
  int x1 = x0 + dir * 4;
  int x2 = x0 + dir * 7;

  // Four line segments form a clean parenthesis-like broadcast wave.
  frame.drawLine(x0, cy - 14, x1, cy - 10, color);
  frame.drawLine(x1, cy - 10, x2, cy, color);
  frame.drawLine(x2, cy, x1, cy + 10, color);
  frame.drawLine(x1, cy + 10, x0, cy + 14, color);
}

void drawPetRequestOverlay(bool leftSide, uint32_t now) {
  if (!petRequestPromptActive || reaction != R_NONE || sleepRequestPending) return;

  uint32_t elapsed = now - petRequestPromptStart;
  float phase = (float)elapsed / 1000.0f + (leftSide ? 0.0f : 0.15f);

  // Heart pulse: affectionate rather than alarmingly fast.
  float heartPulse = 0.5f + 0.5f * sinf(phase * 4.2f);
  int heartSize = 9 + (int)(heartPulse * 2.5f);
  uint16_t heartColor = blend565(C_BLUSH, C_WHITE, 0.12f + heartPulse * 0.30f);

  // Two parenthesis waves on each side alternate brightness, reading as
  // (( heart )) / attention broadcast without using text or a hand icon.
  float innerWave = 0.5f + 0.5f * sinf(phase * 4.2f);
  float outerWave = 0.5f + 0.5f * sinf(phase * 4.2f - 1.25f);
  uint16_t innerColor = blend565(C_BLUSH, C_WHITE, 0.10f + innerWave * 0.55f);
  uint16_t outerColor = blend565(C_BLUSH, C_WHITE, 0.05f + outerWave * 0.45f);

  const int cx = 100;
  const int cy = 164;

  drawPetRequestHeart(cx, cy, heartSize, heartColor);

  drawPetRequestParen(cx, cy, 21, true, innerColor);
  drawPetRequestParen(cx, cy, 21, false, innerColor);
  drawPetRequestParen(cx, cy, 33, true, outerColor);
  drawPetRequestParen(cx, cy, 33, false, outerColor);

  // Tiny center flash reinforces the request rhythm while remaining soft.
  if (heartPulse > 0.72f) {
    frame.fillCircle(cx, cy - 4, 2, C_WHITE);
  }
}

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

  float sleepRequestLid = sleepRequestLidAmount(now);
  if (sleepRequestLid > effectiveBlink) effectiveBlink = sleepRequestLid;

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

#if TANDO_WOKWI_MODE
uint16_t *renderBackgroundRow(uint16_t y) {
  const uint16_t halfHeight = FRAME_H / 2;
  return renderBackground[y / halfHeight] + (y % halfHeight) * FRAME_W;
}
void restoreRenderBackground(TandoFrameRegion region) {
  for (uint16_t y = region.y; y < region.y + region.height; y++) {
    memcpy(frame.getBuffer() + y * FRAME_W + region.x,
           renderBackgroundRow(y) + region.x, region.width * sizeof(uint16_t));
  }
}
TandoRingBackgroundKey getRenderBackgroundKey(uint32_t now) {
  TandoRingBackgroundKey key = {};
  key.visible = reaction == R_NONE;
  if (key.visible) {
    float pulse = getRingPulseAmount(now);
    key.credits = visualCredits;
    key.thickness = pulse > 0.35f ? 4 : 3;
    key.color = blend565(getRingColor(), C_WHITE, pulse * 0.55f);
  }
  return key;
}
void pushFrame(bool leftSide) {
  uint16_t *buffer = frame.getBuffer();
  Adafruit_GC9A01A &display = leftSide ? leftDisplay : rightDisplay;
  uint32_t simProbeStart = micros();
  uint16_t *previous = leftSide ? leftFrameCache : rightFrameCache;
  bool &valid = leftSide ? leftFrameCacheValid : rightFrameCacheValid;
  TandoFramePlan plan = {};
  if (simulatorUseBands) {
    plan = tando_changed_bands_in(buffer, valid ? previous : nullptr,
                                 FRAME_W, FRAME_H, renderDiffRegion);
  } else {
    TandoFrameRegion region = tando_changed_region_in(buffer, valid ? previous : nullptr,
                                                     FRAME_W, FRAME_H, renderDiffRegion);
    if (region.width && region.height) { plan.regions[0] = region; plan.count = 1; }
  }
  if (!plan.count) {
    simProbeSpiUs += micros() - simProbeStart;
    return;
  }

  // Both displays render sequentially, so they can reuse this fixed buffer.
  alignas(4) static uint16_t packedRows[800];
  display.startWrite();
  for (uint8_t i = 0; i < plan.count; i++) {
    TandoFrameRegion region = plan.regions[i];
    display.setAddrWindow(FRAME_X + region.x, FRAME_Y + region.y, region.width, region.height);
    if (region.width == FRAME_W) {
      display.writePixels(buffer + region.y * FRAME_W, region.width * region.height);
    } else {
      uint16_t rowsPerChunk = sizeof(packedRows) / (region.width * sizeof(uint16_t));
      for (uint16_t y = region.y; y < region.y + region.height; ) {
        uint16_t rows = region.y + region.height - y;
        if (rows > rowsPerChunk) rows = rowsPerChunk;
        for (uint16_t row = 0; row < rows; row++) {
          memcpy(packedRows + row * region.width, buffer + (y + row) * FRAME_W + region.x,
                 region.width * sizeof(uint16_t));
        }
        display.writePixels(packedRows, rows * region.width);
        y += rows;
      }
    }
    simProbeTransferBytes += region.width * region.height * sizeof(uint16_t);
  }
  display.endWrite();
  if (previous) {
    for (uint8_t i = 0; i < plan.count; i++) {
      TandoFrameRegion region = plan.regions[i];
      if (region.width == FRAME_W) {
        memcpy(previous + region.y * FRAME_W, buffer + region.y * FRAME_W,
               region.width * region.height * sizeof(uint16_t));
      } else {
        for (uint16_t y = region.y; y < region.y + region.height; y++) {
          memcpy(previous + y * FRAME_W + region.x, buffer + y * FRAME_W + region.x,
                 region.width * sizeof(uint16_t));
        }
      }
    }
    valid = true;
  }
  simProbeSpiUs += micros() - simProbeStart;
}
#endif

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
#if TANDO_WOKWI_MODE // TANDO_PERF_PROBE
  uint32_t simProbeStart = micros();
#endif // TANDO_PERF_PROBE
#if TANDO_WOKWI_MODE
  TandoRingBackgroundKey backgroundKey = getRenderBackgroundKey(now);
  // The fixed ring palette is opaque and non-black; other palettes use full rendering.
  bool useBackground = renderBackground[0] && renderBackground[1] &&
                       frame.getRotation() == 0 && C_BLACK == 0 && C_RING_TRACK != 0 &&
                       (!backgroundKey.visible || backgroundKey.color != C_BLACK);
  renderDiffRegion = {0, 0, FRAME_W, FRAME_H};
  if (useBackground) {
    if (!renderBackgroundValid || !(backgroundKey == renderBackgroundKey)) {
      frame.fillScreen(C_BLACK);
      if (backgroundKey.visible) drawProgressRing(now);
      for (uint16_t y = 0; y < FRAME_H; y++) {
        memcpy(renderBackgroundRow(y), frame.getBuffer() + y * FRAME_W,
               FRAME_W * sizeof(uint16_t));
      }
      renderBackgroundKey = backgroundKey;
      renderBackgroundValid = true;
    } else {
      restoreRenderBackground(lastCanvasDynamic);
    }
    frame.beginTracking();
  } else {
    renderBackgroundValid = false;
    frame.fillScreen(C_BLACK);
  }
#else
  frame.fillScreen(C_BLACK);
#endif

  // Hunger no longer replaces the face. The normal eye keeps its full state
  // machine (idle, Blink, Wink, Smile, Play and user reactions).
  drawFantasyEye(leftSide, now);
  drawSleepGraphics(leftSide, now);
  drawSleepRequestGraphics(leftSide, now);
  drawCompletionSparkles(leftSide, now);

  if (reaction == R_NONE && !sleepRequestPending && hungerPromptActive) {
    drawHungerFoodOverlay(leftSide, now);
  }

  if (reaction == R_NONE && !sleepRequestPending && petRequestPromptActive) {
    drawPetRequestOverlay(leftSide, now);
  }

  // During an actual reaction, show only that reaction's eye/graphics.
  // The persistent Progress Ring returns immediately after the reaction ends.
#if TANDO_WOKWI_MODE
  TandoEyeRenderState &eyeState = eyeRenderState[leftSide ? 0 : 1];
  if (useBackground) {
    TandoFrameRegion dynamic = frame.endTracking();
    // Preserve the original ring-LAST occlusion where dynamic graphics overlap it.
    if (backgroundKey.visible) {
      for (uint16_t y = dynamic.y; y < dynamic.y + dynamic.height; y++) {
        const uint16_t *background = renderBackgroundRow(y);
        uint16_t *pixels = frame.getBuffer() + y * FRAME_W;
        for (uint8_t half = 0; half < 2; half++) {
          const ProgressRingRowSpan &span = progressRingRowSpans[y][half];
          uint16_t first = dynamic.x > span.first ? dynamic.x : span.first;
          uint16_t right = dynamic.x + dynamic.width;
          uint16_t end = right < span.end ? right : span.end;
          for (uint16_t x = first; x < end; x++) {
            if (background[x] != C_BLACK) pixels[x] = background[x];
          }
        }
      }
    }
    if (eyeState.keyValid && backgroundKey == eyeState.key) {
      renderDiffRegion = tando_union_region(eyeState.dynamic, dynamic);
    }
    eyeState.key = backgroundKey;
    eyeState.keyValid = true;
    eyeState.dynamic = dynamic;
    lastCanvasDynamic = dynamic;
  } else {
    eyeState.keyValid = false;
    if (reaction == R_NONE) drawProgressRing(now);
  }
#else
  if (reaction == R_NONE) {
    drawProgressRing(now);
  }
#endif

#if TANDO_WOKWI_MODE // TANDO_PERF_PROBE
  simProbeDrawUs += micros() - simProbeStart;
#endif // TANDO_PERF_PROBE
#if TANDO_WOKWI_MODE
  pushFrame(leftSide);
#else
  if (leftSide) {
    pushLeft();
  } else {
    pushRight();
  }
#endif
}

#if TANDO_WOKWI_MODE
void printSimulatorConfiguration() {
  Serial.printf("SIM CONFIG: cpu_mhz=%u apb_hz=%lu spi_div=0x%lX psram_bytes=%lu canvas=%p cache_l=%p cache_r=%p\r\n",
                (unsigned)getCpuFrequencyMhz(), (unsigned long)getApbFrequency(),
                (unsigned long)displaySPI.getClockDivider(), (unsigned long)ESP.getPsramSize(),
                (void *)frame.getBuffer(), (void *)leftFrameCache, (void *)rightFrameCache);
}
#endif

void setupEyes() {
  // ----------------------------------------------------------
  // Canvas
  // ----------------------------------------------------------
  if (frame.getBuffer() == nullptr) {
    Serial.println("FATAL: CANVAS ALLOCATION FAILED");
    while (true) delay(1000);
  }

#if TANDO_WOKWI_MODE
  initProgressRingGeometry();
#endif

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
}
