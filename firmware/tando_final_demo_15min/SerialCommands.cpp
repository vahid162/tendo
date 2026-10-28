#include "SerialCommands.h"
#include "TandoConfig.h"
#include "Demo.h"
#include "Reactions.h"
#include "CareRequests.h"
#include "Eyes.h"
#include "Touch.h"

// Serial diagnostics and previews call the same subsystem APIs as real inputs.

void printDemoStatus(uint32_t now) {
  uint32_t elapsed = getActiveElapsedMs(now);
  uint32_t stageElapsed = getStageActiveElapsedMs(now);

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
  Serial.print("Stage-local active time: ");
  Serial.print(stageElapsed / 60000UL);
  Serial.print("m ");
  Serial.print((stageElapsed / 1000UL) % 60UL);
  Serial.println("s");
  Serial.print("Sleep credit window: ");
  Serial.println(sleepCreditWindowOpen(now) ? "OPEN" : "CLOSED");
  Serial.print("Sleep credited this Stage: ");
  Serial.println(sleepCreditedThisStage() ? "YES" : "NO");
  Serial.print("Sleep Request pending: ");
  Serial.println(sleepRequestPending ? "YES" : "NO");
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
  Serial.print(" nextSlot=");
  Serial.print((hungerRequestSlot < HUNGER_REQUESTS_PER_STAGE)
    ? (hungerRequestSlot + 1)
    : HUNGER_REQUESTS_PER_STAGE);
  Serial.print("/10 scheduler=");
  if (completionFlag ||
      hungerNeedSatisfiedThisStage() ||
      hungerRequestSlot >= HUNGER_REQUESTS_PER_STAGE) {
    Serial.println("STOPPED");
  } else {
    Serial.print("RUNNING");
    if (nextHungerRequestAt != 0) {
      uint32_t careWall = getCurrentCareStageWallMs(now);
      uint32_t nextIn = (nextHungerRequestAt > careWall)
        ? (nextHungerRequestAt - careWall)
        : 0;
      Serial.print(" nextIn=");
      Serial.print(nextIn / 1000UL);
      Serial.print("s");
    }
    Serial.println();
  }

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
  Serial.print(" nextSlot=");
  Serial.print((petRequestSlot < PET_REQUESTS_PER_STAGE)
    ? (petRequestSlot + 1)
    : PET_REQUESTS_PER_STAGE);
  Serial.print("/10 scheduler=");
  if (completionFlag ||
      petRequestNeedSatisfiedThisStage() ||
      petRequestSlot >= PET_REQUESTS_PER_STAGE) {
    Serial.println("STOPPED");
  } else {
    Serial.print("RUNNING");
    if (nextPetRequestAt != 0) {
      uint32_t careWall = getCurrentCareStageWallMs(now);
      uint32_t nextIn = (nextPetRequestAt > careWall)
        ? (nextPetRequestAt - careWall)
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
        if (reaction == R_NONE && !sleepRequestPending) {
          startBlink(now);
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

#if TANDO_WOKWI_MODE
      case 'f':
        toggleSimulatorFrameInterval();
        break;
      case 'o':
        toggleSimulatorTransferMode();
        break;
      case 'v':
        printSimProbe();
        break;
#endif

      case '?':
        printSerialHelp();
        break;
    }
  }
}
