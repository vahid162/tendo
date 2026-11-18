#include "DemoController.h"
#include <esp_system.h>
#include "../TandoConfig.h"
#include "../model/Demo.h"
#include "../model/Reaction.h"
#include "../model/Request.h"
#include "../view/EyesView.h"
#include "../Touch.h"
#include "../Rfid.h"
#include "../SerialCommands.h"

#define TANDO_VERSION_MAJOR 0
#define TANDO_VERSION_MINOR 10
#define TANDO_VERSION_PATCH 0
#define TANDO_VERSION "0.10.0-rc.18"

//these should be global...not in any classes
//this can be done by dependency injection or other ways but for now let's stick to this one.
DemoController demoController;
FoodReaction foodReaction;
PetReaction petReaction;
ConfusedReaction confusedReaction;
CompletionReaction completionReaction;
UnlockReaction unlockReaction;
SleepReaction sleepReaction;
LEDReaction ledReaction;
HungerRequest hungerRequest("HUNGER");
PetRequest petRequest("PET");


//TODO: should think of priority thing in the controller, not in other classes.
// bool DemoController::faceBlockedForRequests() {
//   return reactionIsBusy()
//       || reaction == R_SLEEP 
//       || demo.sleepRequestPending();
// }

void DemoController::initCareClock(uint32_t now, bool ready) {
  careStageWallStartMs = ready ? now : 0;
  careStageWallClockReady = ready;
  careRequestNextAllowedWallMs = 0;
}

void DemoController::printBanner() {
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
}

void DemoController::boot() {
  Serial.begin(115200);
  delay(500);

  randomSeed((uint32_t)esp_random());

  eyesView.setupEyes();

  setupReactionLed();
  Serial.println("REACTION LED READY");

#if !TANDO_WOKWI_MODE
  setupTouch();
  setupRfid();
#else
  Serial.println("SIM: Touch/RFID use Serial controls; physical sensor polling is disabled");
#endif

  uint32_t now = millis();
  demo.begin(now);
  initCareClock(now, false);

  eyesModel.chooseIdleLook(now);
  eyesModel.scheduleNextBlink(now);
  eyesModel.scheduleNextAutonomous(now);
  hungerRequest.syncRequestStage(now);
  petRequest.syncRequestStage(now);

  previousFrameTime = now;

  printBanner();

  printDemoStatus(now);
  printSerialHelp();
  
#if TANDO_WOKWI_MODE
  eyesView.setupSimulatorCaches();
  eyesView.printSimulatorConfiguration();
#endif
}

void DemoController::resetDemo(uint32_t now) {
  demo.reset(now);                 // مدل + NVS + request ها
  initCareClock(now, true);
  pendingUnlockMask = 0;
  completionReaction.pending = false;
}

void DemoController::update() {
  uint32_t now = millis();
#if TANDO_WOKWI_MODE
  eyesView.noteSimulatorLoop(now);
#endif

  // Serial test / presentation tools.
  updateSerial(now);

  // Update active demo timer + stage gates + NVS checkpoint.
  demo.updateDemoClock(now);

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
  demo.updateSleepNeedState(now, sleepReaction.sleepTagPresent);

  // Care Requests use a Stage-local wall-clock timeline.
  hungerRequest.updateRequestScheduler(now);
  petRequest.updateRequestScheduler(now);

  // Generic autonomous personality remains the lowest-priority eye behavior.
  eyesModel.updateAutonomous(now);

  // Micro idle gaze continues between larger personality expressions.
  if (reaction == R_NONE &&
      eyesModel.autonomousState.type == AUTO_NONE &&
      !demo.sleepRequestPending &&
      (int32_t)(now - eyesModel.nextIdleLook) >= 0) {
    eyesModel.chooseIdleLook(now);
  }

  eyesModel.updateBlink(now);
#if !TANDO_WOKWI_MODE
  eyesModel.updateEyeTargets(now);
#endif
  updateReactionLed(now);

  // ----------------------------------------------------------
  // ~29 FPS render limit
  // ----------------------------------------------------------
#if TANDO_WOKWI_MODE
  if ((now - lastFrame) < eyesView.simulatorFrameIntervalMs) {
#else
  if ((now - lastFrame) < FRAME_MS) {
#endif
#if TANDO_WOKWI_MODE
    eyesView.noteSimulatorWait();
    // Yield only after inputs and state updates have been serviced.
    delay(1);
#endif
    return;
  }

  lastFrame = now;

  float dt = (float)(now - previousFrameTime) / 1000.0f;
  previousFrameTime = now;

  if (dt > 0.10f) dt = 0.10f;

#if TANDO_WOKWI_MODE
  // Targets have no consumers until this frame's dynamics update.
  eyesModel.updateEyeTargets(now);
#endif
  eyesModel.updateEyeDynamics(dt);

#if TANDO_WOKWI_MODE
  eyesView.beginSimulatorFrame();
#endif
  eyesView.renderDisplay(true, now);
  eyesView.renderDisplay(false, now);
#if TANDO_WOKWI_MODE
  eyesView.endSimulatorFrame();
#endif
}