#include "Demo.h"
#include "../TandoConfig.h"
#include "Reaction.h"
#include "Request.h"
#include "../view/EyesView.h"
#include "../controller/DemoController.h"


Demo demo;

bool Demo::sleepCreditWindowOpen(uint32_t now) {
  if (completed) return false;

  uint32_t stageElapsed = clock.getStageActiveElapsedMs(now, this->stage);
  //TODO: these constants should not be available from this file
  return stageElapsed >= SLEEP_CREDIT_WINDOW_START_MS &&
         stageElapsed < STAGE_MS;
}

bool Demo::sleepRequestStartReached(uint32_t now) {
  if (completed) return false;

  uint32_t stageElapsed = clock.getStageActiveElapsedMs(now, this->stage);
  return stageElapsed >= SLEEP_REQUEST_START_MS &&
         stageElapsed < STAGE_MS;
}

bool Demo::sleepCreditedThisStage() {
  return (stageCareMask & CARE_SLEEP_BIT) != 0;
}

void Demo::save(uint32_t now) {
  DemoSnapshot snapshot;
  snapshot.elapsedMs = clock.getActiveElapsedMs(now);
  snapshot.stage = stage;
  snapshot.credits = visualCredits;
  snapshot.careMask = stageCareMask;
  snapshot.started = clock.started;
  snapshot.completed = completed;
  snapshot.hungerStage = hungerRequest.requestStage;
  snapshot.hungerShown = hungerRequest.requestsShown;
  snapshot.petStage = petRequest.requestStage;
  snapshot.petShown = petRequest.requestsShown;
  storage.save(snapshot);
  clock.lastNvsCheckpointElapsed = snapshot.elapsedMs;
}

void Demo::begin(uint32_t now) {
  storage.begin();
  restore(now);
}


void Demo::reset(uint32_t now) {
  storage.clear();
  clock.reset(now);
  stage = 1;
  stageCareMask = 0;
  visualCredits = 0;
  completed = false;
  resetTransient();
  hungerRequest.reset();
  hungerRequest.requestStage = 1;
  petRequest.reset();
  petRequest.requestStage = 1;

  save(now);
  Serial.println();
  Serial.println("*** DEMO STATE RESET ***");
  Serial.println("Next user interaction starts Stage 1 timer.");
}


void Demo::restore(uint32_t now) {
  DemoSnapshot snapshot;
  // if (!storage.load(snapshot)) {
  //   reset(now);
  //   return;
  // }
  storage.load(snapshot);

  stage = (snapshot.stage >= 1 && snapshot.stage <= 3) ? snapshot.stage : 1;
  visualCredits = snapshot.credits;
  if (visualCredits > TOTAL_VISUAL_CREDITS) visualCredits = TOTAL_VISUAL_CREDITS;
    
  stageCareMask = snapshot.careMask;
  completed = snapshot.completed;
  clock.restore(snapshot.elapsedMs, snapshot.started, now);
  resetTransient();

  if (snapshot.hungerShown > 10)
    snapshot.hungerShown = 10;
  if (snapshot.petShown > 10)
    snapshot.petShown = 10;
  hungerRequest.restore(snapshot.hungerStage, snapshot.hungerShown);
  petRequest.restore(snapshot.petStage, snapshot.petShown);

  if (clock.savedActiveMs >= TOTAL_DEMO_MS) completed = true;

  if (completed) { 
    clock.finish(); 
    stage = 3;
  }

  // Validate Stage against persisted active time after an interrupted power cycle.
  if (!completed) {
    if (clock.savedActiveMs >= 2UL * STAGE_MS) {
      if (stage < 3) {
        stage = 3;
        stageCareMask = 0;
        resetTransient();
      }
    } else if (clock.savedActiveMs >= STAGE_MS) {
      if (stage < 2) {
        stage = 2;
        stageCareMask = 0;
        resetTransient();
      }
    }
  }
  clock.lastNvsCheckpointElapsed = clock.getActiveElapsedMs(now);

}



void Demo::notifyUserInteraction(uint32_t now) {
  clock.lastInteractionMs = now;

  if (completed) {
    return;
  }

  if (!clock.started) {
    clock.started = true;
    clock.savedActiveMs = 0;
    clock.lastNvsCheckpointElapsed = 0;
    Serial.println("DEMO CLOCK STARTED");
  }

  if (!clock.running) {
    clock.running = true;
    clock.activeRunStartMs = now;
    Serial.println("DEMO CLOCK RESUMED");
  }
}

void Demo::printProgressState() {
  float pct = ((float)visualCredits / (float)TOTAL_VISUAL_CREDITS) * 100.0f;

  Serial.print("STAGE: ");
  Serial.print(stage);
  Serial.print(" | PROGRESS: ");
  Serial.print(visualCredits);
  Serial.print("/9 = ");
  Serial.print(pct, 1);
  Serial.println("%");
}

bool Demo::registerCareCredit(uint8_t careBit, const char *label, uint32_t now) {
  if (completed) {
    Serial.print(label);
    Serial.println(" -> REACTION ONLY (DEMO COMPLETE)");
    return false;
  }

  if ((stageCareMask & careBit) != 0) {
    Serial.print(label);
    Serial.println(" -> REACTION ONLY (ALREADY CREDITED IN THIS STAGE)");
    return false;
  }

  uint8_t stageCap = stage * 3;

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

  eyesModel.triggerRingPulse(now);
  save(now);

  Serial.print(label);
  Serial.println(" -> +1 PROGRESS");
  printProgressState();

  return true;
}

void Demo::autoFillPetAndFoodCreditsForClosingStage() {
  const uint8_t autoFillMask = CARE_PET_BIT | CARE_FOOD_BIT;
  // this must be upgraded so it can handle other requests too.
  uint8_t missing = autoFillMask & (uint8_t)~stageCareMask;

  if (missing != 0) {
    uint8_t added = 0;
    if (missing & CARE_PET_BIT) added++;
    if (missing & CARE_FOOD_BIT) added++;

    stageCareMask |= missing;
    visualCredits += added;
    if (visualCredits > TOTAL_VISUAL_CREDITS) {
      visualCredits = TOTAL_VISUAL_CREDITS;
    }

    Serial.print("STAGE ");
    Serial.print(stage);
    Serial.print(" AUTO-FILL ->");
    if (missing & CARE_PET_BIT) Serial.print(" PET");
    if (missing & CARE_FOOD_BIT) Serial.print(" FOOD");
    Serial.println(" (SLEEP NOT AUTO-FILLED)");
  } else if (!sleepCreditedThisStage()) {
    Serial.print("STAGE ");
    Serial.print(stage);
    Serial.println(" CLOSE -> SLEEP NOT AUTO-FILLED");
  }
}

bool Demo::grantSleepCreditForAcceptedTag(uint32_t now) {
  if (sleepCreditedThisStage() || completed) {
    return registerCareCredit(CARE_SLEEP_BIT, "SLEEP", now);
  }

  if (!sleepCreditWindowOpen(now)) {
    Serial.println("SLEEP -> REACTION ONLY (BEFORE 06:00)");
    return false;
  }

  bool granted = registerCareCredit(CARE_SLEEP_BIT, "SLEEP", now);
  if (granted && sleepRequestPending) {
    sleepRequestPending = false;
    Serial.println("SLEEP REQUEST RESOLVED");
  }

  return granted;
}

void Demo::updateSleepNeedState(uint32_t now, bool sleeping) {
  if (completed) {
    sleepRequestPending = false;
    return;
  }

  bool creditWindowOpen = sleepCreditWindowOpen(now);
  if (creditWindowOpen && sleepCreditWindowAnnouncedStage != stage) {
    sleepCreditWindowAnnouncedStage = stage;
    Serial.print("SLEEP CREDIT WINDOW OPEN - STAGE ");
    Serial.println(stage);
  }

  // Required held-tag edge case: do not make the user remove and re-present a
  // SLEEP tag that was already keeping Tando in persistent R_SLEEP at 06:00.
  if (creditWindowOpen &&
      !sleepCreditedThisStage() &&
      reaction == R_SLEEP &&
      sleeping) {
    if (registerCareCredit(CARE_SLEEP_BIT, "SLEEP", now)) {
      if (sleepRequestPending) {
        sleepRequestPending = false;
        Serial.println("SLEEP REQUEST RESOLVED");
      }
      Serial.println("SLEEP CREDIT GRANTED FROM ACTIVE HELD TAG");
    }
  }

  if (sleepCreditedThisStage()) {
    sleepRequestPending = false;
    return;
  }

  if (sleepRequestStartReached(now) &&
      reaction != R_SLEEP &&
      !sleepRequestPending) {
    sleepRequestPending = true;

    // Sleep Request is a non-interaction visual. It never starts the clock or
    // pulses the LED, but it immediately takes precedence over care overlays
    // and autonomous personality.
    if (hungerRequest.promptActive) hungerRequest.interruptTrackedRequestPromptForRetry(now);
    if (petRequest.promptActive) petRequest.interruptTrackedRequestPromptForRetry(now);
    eyesModel.cancelAutonomous();
    clearBlinkForExclusiveReaction();

    Serial.print("SLEEP REQUEST ARMED - STAGE ");
    Serial.println(stage);
  }
}

void Demo::updateStageByTime(uint32_t now) {
  if (!clock.started || completed) return;

  uint32_t elapsed = clock.getActiveElapsedMs(now);

  if (elapsed >= TOTAL_DEMO_MS) {
    // Final boundary retains PET/FOOD auto-fill only. A missed SLEEP remains
    // missing so Completion never synthesizes a credit that did not happen.
    autoFillPetAndFoodCreditsForClosingStage();

    completed = true;
    clock.running = false;
    clock.savedActiveMs = TOTAL_DEMO_MS;
    stage = 3;
    resetTransient();

    eyesModel.triggerRingPulse(now);
    pendingUnlockMask = 0;  // no stale Stage 2/3 animation after completion
    completionReaction.pending = true;
    save(now);
    return;
  }

  uint8_t targetStage = 1;
  if (elapsed >= 2UL * STAGE_MS) {
    targetStage = 3;
  } else if (elapsed >= STAGE_MS) {
    targetStage = 2;
  }

  if (targetStage > stage) {
    // Close the old Stage before replacing its care mask. Only PET/FOOD may
    // follow the existing boundary auto-fill policy; SLEEP never does.
    autoFillPetAndFoodCreditsForClosingStage();

    stage = targetStage;
    stageCareMask = 0;
    resetTransient();
    hungerRequest.resetRequestForStage(now);
    petRequest.resetRequestForStage(now);

    if (stage == 2) {
      pendingUnlockMask |= PENDING_UNLOCK_STAGE2;
    } else if (stage == 3) {
      pendingUnlockMask |= PENDING_UNLOCK_STAGE3;
    }

    eyesModel.triggerRingPulse(now);
    eyesModel.chooseIdleLook(now);
    save(now);

    Serial.println();
    Serial.print("TIME GATE -> STAGE ");
    Serial.println(stage);
    printProgressState();
  }
}

void Demo::updateDemoClock(uint32_t now) {
  if (!clock.started || completed) {
    return;
  }

  if (clock.running) {
    if ((now - clock.lastInteractionMs) >= INACTIVITY_PAUSE_MS) {
      clock.savedActiveMs = clock.getActiveElapsedMs(now);
      clock.running = false;
      clock.activeRunStartMs = 0;
      save(now);

      Serial.println("DEMO CLOCK PAUSED - waiting for interaction");
    }
  }

  updateStageByTime(now);

  uint32_t elapsed = clock.getActiveElapsedMs(now);

  if (clock.running && (elapsed - clock.lastNvsCheckpointElapsed) >= NVS_CHECKPOINT_MS)
    save(now);
}


void Demo::resetTransient() {
  sleepRequestPending = false;
  sleepCreditWindowAnnouncedStage = 0;
  // careStageWallStartMs = 0;
  // careStageWallClockReady = false;
  // careRequestNextAllowedWallMs = 0;
}
