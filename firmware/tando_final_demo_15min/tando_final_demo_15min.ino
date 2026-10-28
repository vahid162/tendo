#include <Arduino.h>
#include <esp_system.h>
#include "TandoConfig.h"
#include "Demo.h"
#include "Reactions.h"
#include "CareRequests.h"
#include "Eyes.h"
#include "Touch.h"
#include "Rfid.h"
#include "SerialCommands.h"

#define TANDO_VERSION_MAJOR 0
#define TANDO_VERSION_MINOR 10
#define TANDO_VERSION_PATCH 0
#define TANDO_VERSION "0.10.0-rc.13"

// Tando firmware entry point; subsystem state is owned by its modules.
uint32_t lastFrame = 0;
uint32_t lastTouchPoll = 0;
uint32_t lastRfidPoll = 0;
uint32_t previousFrameTime = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

  randomSeed((uint32_t)esp_random());

  setupEyes();

  setupReactionLed();
  Serial.println("REACTION LED READY");

#if !TANDO_WOKWI_MODE
  setupTouch();
  setupRfid();
#else
  Serial.println("SIM: Touch/RFID use Serial controls; physical sensor polling is disabled");
#endif
  setupDemoStorage();

  uint32_t now = millis();
  restoreDemoRuntime(now);

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
#if TANDO_WOKWI_MODE
  setupSimulatorCaches();
#endif
}

void loop() {
  uint32_t now = millis();
#if TANDO_WOKWI_MODE
  noteSimulatorLoop(now);
#endif

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
#if !TANDO_WOKWI_MODE
  if ((now - lastTouchPoll) >= TOUCH_POLL_MS) {
    lastTouchPoll = now;
    updateTouch(now);
  }

  if ((now - lastRfidPoll) >= RFID_POLL_MS) {
    lastRfidPoll = now;
    updateRFID(now);
  }
#endif

  servicePendingSystemReaction(now);
  servicePendingUserReaction(now);

  // SLEEP credit/request thresholds use only Stage-local Active Demo Time.
  // This runs after inputs so an accepted tag at 06:00+ can resolve the need
  // in the same loop, and it never resumes the demo clock by itself.
  updateSleepNeedState(now);

  // Care Requests use a Stage-local wall-clock timeline. They can run while
  // Tando is idle or the Active Demo clock is paused. Stage/Need state still
  // controls quota, and Hunger/PET Request never overlap.
  updateHungerRequestScheduler(now);
  updatePetRequestScheduler(now);

  // Generic autonomous personality remains the lowest-priority eye behavior.
  updateAutonomous(now);

  // Micro idle gaze continues between larger personality expressions.
  if (reaction == R_NONE &&
      autonomousState == AUTO_NONE &&
      !sleepRequestPending &&
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
#if TANDO_WOKWI_MODE
    noteSimulatorWait();
    // Yield only after inputs and state updates have been serviced.
    delay(1);
#endif
    return;
  }

  lastFrame = now;

  float dt = (float)(now - previousFrameTime) / 1000.0f;
  previousFrameTime = now;

  if (dt > 0.10f) dt = 0.10f;

  updateEyeDynamics(dt);

#if TANDO_WOKWI_MODE
  beginSimulatorFrame();
#endif
  renderDisplay(true, now);
  renderDisplay(false, now);
#if TANDO_WOKWI_MODE
  endSimulatorFrame();
#endif
}
