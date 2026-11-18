#include "SerialCommands.h"
#include "./model/Demo.h"
#include "./model/Reaction.h"
#include "./model/Request.h"
#include "./view/EyesView.h"
#include "Touch.h"
#include "./controller/DemoController.h"

// Serial diagnostics and previews call the same subsystem APIs as real inputs.

void printDemoStatus(uint32_t now) {
  uint32_t elapsed = demo.clock.getActiveElapsedMs(now);
  uint32_t stageElapsed = demo.clock.getStageActiveElapsedMs(now, demo.stage);

  Serial.println();
  Serial.println("---------- TANDO STATUS ----------");
  Serial.print("Demo started: ");
  Serial.println(demo.clock.started ? "YES" : "NO");
  Serial.print("Clock running: ");
  Serial.println(demo.clock.running ? "YES" : "NO / PAUSED");
  Serial.print("Active time: ");
  Serial.print(elapsed / 60000UL);
  Serial.print("m ");
  Serial.print((elapsed / 1000UL) % 60UL);
  Serial.println("s");
  Serial.print("Stage: ");
  Serial.println(demo.stage);
  Serial.print("Stage-local active time: ");
  Serial.print(stageElapsed / 60000UL);
  Serial.print("m ");
  Serial.print((stageElapsed / 1000UL) % 60UL);
  Serial.println("s");
  Serial.print("Sleep credit window: ");
  Serial.println(demo.sleepCreditWindowOpen(now) ? "OPEN" : "CLOSED");
  Serial.print("Sleep credited this Stage: ");
  Serial.println(demo.sleepCreditedThisStage() ? "YES" : "NO");
  Serial.print("Sleep Request pending: ");
  Serial.println(demo.sleepRequestPending ? "YES" : "NO");
  Serial.print("Current-stage care mask: 0x");
  Serial.println(demo.stageCareMask, HEX);
  Serial.print("Progress credits: ");
  Serial.print(demo.visualCredits);
  Serial.println(" / 9");
  Serial.print("Complete: ");
  Serial.println(demo.completed ? "YES" : "NO");
  Serial.print("Autonomous state: ");
  Serial.println(eyesModel.autonomousState.getName(eyesModel.autonomousState.type));
  Serial.print("Hunger requests: stage=");
  Serial.print(hungerRequest.requestStage);
  Serial.print(" completed=");
  Serial.print(hungerRequest.requestsShown);
  Serial.print("/");
  Serial.print(HUNGER_REQUESTS_PER_STAGE);
  Serial.print(" active=");
  Serial.print(hungerRequest.promptActive ? "YES" : "NO");
  Serial.print(" foodSatisfied=");
  Serial.print(hungerRequest.needSatisfiedThisStage() ? "YES" : "NO");
  Serial.print(" nextSlot=");
  Serial.print((hungerRequest.requestSlot < HUNGER_REQUESTS_PER_STAGE)
    ? (hungerRequest.requestSlot + 1)
    : HUNGER_REQUESTS_PER_STAGE);
  Serial.print("/10 scheduler=");
  if (demo.completed ||
      hungerRequest.needSatisfiedThisStage() ||
      hungerRequest.requestSlot >= HUNGER_REQUESTS_PER_STAGE) {
    Serial.println("STOPPED");
  } else {
    Serial.print("RUNNING");
    if (hungerRequest.nextRequestAt != 0) {
      uint32_t careWall = getCurrentCareStageWallMs(now);
      uint32_t nextIn = (hungerRequest.nextRequestAt > careWall)
        ? (hungerRequest.nextRequestAt - careWall)
        : 0;
      Serial.print(" nextIn=");
      Serial.print(nextIn / 1000UL);
      Serial.print("s");
    }
    Serial.println();
  }

  Serial.print("PET requests: stage=");
  Serial.print(petRequest.requestStage);
  Serial.print(" completed=");
  Serial.print(petRequest.requestsShown);
  Serial.print("/");
  Serial.print(PET_REQUESTS_PER_STAGE);
  Serial.print(" active=");
  Serial.print(petRequest.promptActive ? "YES" : "NO");
  Serial.print(" petSatisfied=");
  Serial.print(petRequest.needSatisfiedThisStage() ? "YES" : "NO");
  Serial.print(" nextSlot=");
  Serial.print((petRequest.requestSlot < PET_REQUESTS_PER_STAGE)
    ? (petRequest.requestSlot + 1)
    : PET_REQUESTS_PER_STAGE);
  Serial.print("/10 scheduler=");
  if (demo.completed ||
      petRequest.needSatisfiedThisStage() ||
      petRequest.requestSlot >= PET_REQUESTS_PER_STAGE) {
    Serial.println("STOPPED");
  } else {
    Serial.print("RUNNING");
    if (petRequest.nextRequestAt != 0) {
      uint32_t careWall = getCurrentCareStageWallMs(now);
      uint32_t nextIn = (petRequest.nextRequestAt > careWall)
        ? (petRequest.nextRequestAt - careWall)
        : 0;
      Serial.print(" nextIn=");
      Serial.print(nextIn / 1000UL);
      Serial.print("s");
    }
    Serial.println();
  }
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
  Serial.println("r = preview 10 s PET REQUEST ((HEART)) alert (no Stage count)");
  Serial.println("b = blink");
  Serial.println("i = print demo status (including Stage-local SLEEP credit/request state)");
  Serial.println("t = print MPR121 PET E0/E6/E11 raw diagnostics once");
  Serial.println("c = manual MPR121 recalibration only (keep hand away)");
  Serial.println("D = RESET ALL DEMO PROGRESS / TIMER NVS");
#if TANDO_WOKWI_MODE
  Serial.println("v = SIMPERF virtual-time rendering statistics");
  Serial.println("f = toggle SIM frame target 60 / 120 ms");
  Serial.println("o = toggle SIM transfer BANDS / BBOX");
#endif
  Serial.println("? = help");
  Serial.println("======================================");
  Serial.println();
}

void updateSerial(uint32_t now) {
  while (Serial.available()) {
    char c = Serial.read();

    switch (c) {
      case 'p':
        petReaction.handleEvent(now);
        // handlePetEvent(now);
        break;

      case '1':
        foodReaction.handleEvent(1, now);
        // handleFoodEvent(1, now);
        break;

      case '2':
        foodReaction.handleEvent(2, now);
        // handleFoodEvent(2, now);
        break;

      case 's':
        if (reaction == R_SLEEP && demo.sleepRequestPending) {
          // beginSleepWake(now);
          sleepReaction.beginSleepWake(now);
        } else if (!reactionIsBusy()) {
          // handleSleepEvent(now);
          sleepReaction.handleEvent(now);
        }
        break;

      case 'u':
        handleUnknownRfidEvent(now);
        break;

      case 'l':
        if (!reactionIsBusy()) {
          eyesModel.cancelAutonomous();
          eyesModel.startAutonomous(AUTO_LOOK, now);
        }
        break;

      case 'w':
        if (!reactionIsBusy()) {
          eyesModel.cancelAutonomous();
          eyesModel.startAutonomous(AUTO_WINK, now);
        }
        break;

      case 'e':
        if (!reactionIsBusy()) {
          eyesModel.cancelAutonomous();
          eyesModel.startAutonomous(AUTO_SMILE, now);
        }
        break;

      case 'g':
        if (!reactionIsBusy()) {
          eyesModel.cancelAutonomous();
          eyesModel.startAutonomous(AUTO_PLAY, now);
        }
        break;

      case 'h':
        if (!hungerRequest.blockedByPriority() && !petRequest.promptActive) {
          hungerRequest.clearPromptRuntime();
          hungerRequest.retryPending = false;
          hungerRequest.startPrompt(now, false);
        } else {
          Serial.println("HUNGER PREVIEW BLOCKED - CARE/SLEEP/SYSTEM VISUAL ACTIVE");
        }
        break;

      case 'r':
        if (!petRequest.blockedByPriority() && !hungerRequest.promptActive) {
          petRequest.clearPromptRuntime();
          petRequest.retryPending = false;
          petRequest.startPrompt(now, false);
        } else {
          Serial.println("PET REQUEST PREVIEW BLOCKED - CARE/REACTION VISUAL ACTIVE");
        }
        break;

      case 'b':
        if (reaction == R_NONE && !demo.sleepRequestPending) {
          eyesModel.startBlink(now);
        } else {
          Serial.println("BLINK PREVIEW BLOCKED - REACTION/SLEEP REQUEST VISUAL ACTIVE");
        }
        break;

      case 'i':
        printDemoStatus(now);
        break;

      case 't':
#if TANDO_WOKWI_MODE
        Serial.println("SIM: MPR121 is absent. Use p for PET.");
#else
        printMprDiagnostics();
#endif
        break;

      case 'c':
#if TANDO_WOKWI_MODE
        Serial.println("SIM: MPR121 is absent; cannot recalibrate.");
#else
        recalibrateMpr121(now);
#endif
        break;

      case 'D':
        demo.reset(now);
        eyesModel.cancelAutonomous();
        hungerRequest.clearPromptRuntime();
        petRequest.clearPromptRuntime();
        eyesModel.chooseIdleLook(now);
        eyesModel.scheduleNextAutonomous(now);
        hungerRequest.syncRequestStage(now);
        petRequest.syncRequestStage(now);
        eyesModel.triggerRingPulse(now);
        break;

#if TANDO_WOKWI_MODE
      case 'f':
        eyesView.toggleSimulatorFrameInterval();
        break;
      case 'o':
        eyesView.toggleSimulatorTransferMode();
        break;
      case 'v':
        eyesView.printSimProbe();
        break;
#endif

      case '?':
        printSerialHelp();
        break;
    }
  }
}
