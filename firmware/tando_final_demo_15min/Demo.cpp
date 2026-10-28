#include "Demo.h"
#include "TandoConfig.h"
#include "Reactions.h"
#include "CareRequests.h"
#include "Eyes.h"
#include <Preferences.h>

// Owns active time, stage progress, and the persisted NVS snapshot fields.

// Local declarations replace Arduino-generated prototypes.
static bool sleepRequestStartReached(uint32_t now);
static void loadPersistentState();
static void autoFillPetAndFoodCreditsForClosingStage();
static void updateStageByTime(uint32_t now);

static Preferences prefs;
bool demoStarted = false;
bool demoClockRunning = false;
bool completionFlag = false;
static uint32_t savedActiveMs = 0;
static uint32_t activeRunStartMs = 0;
uint32_t lastInteractionMs = 0;
static uint32_t lastNvsCheckpointElapsed = 0;
uint8_t currentStage = 1;
uint8_t stageCareMask = 0;
uint8_t visualCredits = 0;
bool sleepRequestPending = false;
static uint8_t sleepCreditWindowAnnouncedStage = 0;

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

uint32_t getStageActiveElapsedMs(uint32_t now) {
  uint32_t elapsed = getActiveElapsedMs(now);
  uint32_t stageStart = (uint32_t)(currentStage - 1) * STAGE_MS;

  if (elapsed <= stageStart) return 0;

  uint32_t stageElapsed = elapsed - stageStart;
  return (stageElapsed > STAGE_MS) ? STAGE_MS : stageElapsed;
}

bool sleepCreditWindowOpen(uint32_t now) {
  if (completionFlag) return false;

  uint32_t stageElapsed = getStageActiveElapsedMs(now);
  return stageElapsed >= SLEEP_CREDIT_WINDOW_START_MS &&
         stageElapsed < STAGE_MS;
}

bool sleepRequestStartReached(uint32_t now) {
  if (completionFlag) return false;

  uint32_t stageElapsed = getStageActiveElapsedMs(now);
  return stageElapsed >= SLEEP_REQUEST_START_MS &&
         stageElapsed < STAGE_MS;
}

bool sleepCreditedThisStage() {
  return (stageCareMask & CARE_SLEEP_BIT) != 0;
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
    sleepRequestPending = false;
    sleepCreditWindowAnnouncedStage = 0;
    hungerRequestStage = 0;
    hungerRequestsShown = 0;
    nextHungerRequestAt = 0;
    hungerRequestSlot = 0;
    hungerPromptActive = false;
    hungerPromptTracked = false;
    hungerPromptManualPreview = false;
    hungerPromptStart = 0;
    hungerRetryPending = false;
    petRequestStage = 0;
    petRequestsShown = 0;
    nextPetRequestAt = 0;
    petRequestSlot = 0;
    petRequestPromptActive = false;
    petRequestPromptTracked = false;
    petRequestPromptManualPreview = false;
    petRequestPromptStart = 0;
    petRequestRetryPending = false;
    careStageWallStartMs = 0;
    careStageWallClockReady = false;
    careRequestNextAllowedWallMs = 0;
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
  hungerRequestSlot = hungerRequestsShown;
  hungerPromptActive = false;
  hungerPromptTracked = false;
  hungerPromptManualPreview = false;
  hungerPromptStart = 0;
  hungerRetryPending = false;
  nextPetRequestAt = 0;
  petRequestSlot = petRequestsShown;
  petRequestPromptActive = false;
  petRequestPromptTracked = false;
  petRequestPromptManualPreview = false;
  petRequestPromptStart = 0;
  petRequestRetryPending = false;
  careStageWallStartMs = 0;
  careStageWallClockReady = false;
  careRequestNextAllowedWallMs = 0;
  sleepRequestPending = false;
  sleepCreditWindowAnnouncedStage = 0;

  if (currentStage < 1 || currentStage > 3) currentStage = 1;
  if (visualCredits > TOTAL_VISUAL_CREDITS) visualCredits = TOTAL_VISUAL_CREDITS;
  if (hungerRequestsShown > 10) hungerRequestsShown = 10;
  if (petRequestsShown > 10) petRequestsShown = 10;
  if (savedActiveMs > TOTAL_DEMO_MS) savedActiveMs = TOTAL_DEMO_MS;

  if (savedActiveMs >= TOTAL_DEMO_MS) {
    completionFlag = true;
  }

  if (completionFlag) {
    savedActiveMs = TOTAL_DEMO_MS;
    currentStage = 3;
    sleepRequestPending = false;
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
  sleepRequestPending = false;
  sleepCreditWindowAnnouncedStage = 0;

  hungerRequestStage = 1;
  hungerRequestsShown = 0;
  nextHungerRequestAt = 0;
  hungerRequestSlot = 0;
  hungerPromptActive = false;
  hungerPromptTracked = false;
  hungerPromptManualPreview = false;
  hungerPromptStart = 0;
  hungerRetryPending = false;

  petRequestStage = 1;
  petRequestsShown = 0;
  nextPetRequestAt = 0;
  petRequestSlot = 0;
  petRequestPromptActive = false;
  petRequestPromptTracked = false;
  petRequestPromptManualPreview = false;
  petRequestPromptStart = 0;
  petRequestRetryPending = false;
  careStageWallStartMs = now;
  careStageWallClockReady = true;
  careRequestNextAllowedWallMs = 0;

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

void autoFillPetAndFoodCreditsForClosingStage() {
  const uint8_t autoFillMask = CARE_PET_BIT | CARE_FOOD_BIT;
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
    Serial.print(currentStage);
    Serial.print(" AUTO-FILL ->");
    if (missing & CARE_PET_BIT) Serial.print(" PET");
    if (missing & CARE_FOOD_BIT) Serial.print(" FOOD");
    Serial.println(" (SLEEP NOT AUTO-FILLED)");
  } else if (!sleepCreditedThisStage()) {
    Serial.print("STAGE ");
    Serial.print(currentStage);
    Serial.println(" CLOSE -> SLEEP NOT AUTO-FILLED");
  }
}

bool grantSleepCreditForAcceptedTag(uint32_t now) {
  if (sleepCreditedThisStage() || completionFlag) {
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

void updateSleepNeedState(uint32_t now) {
  if (completionFlag) {
    sleepRequestPending = false;
    return;
  }

  bool creditWindowOpen = sleepCreditWindowOpen(now);
  if (creditWindowOpen && sleepCreditWindowAnnouncedStage != currentStage) {
    sleepCreditWindowAnnouncedStage = currentStage;
    Serial.print("SLEEP CREDIT WINDOW OPEN - STAGE ");
    Serial.println(currentStage);
  }

  // Required held-tag edge case: do not make the user remove and re-present a
  // SLEEP tag that was already keeping Tando in persistent R_SLEEP at 06:00.
  if (creditWindowOpen &&
      !sleepCreditedThisStage() &&
      reaction == R_SLEEP &&
      sleepTagPresent) {
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
    if (hungerPromptActive) interruptTrackedHungerPromptForRetry(now);
    if (petRequestPromptActive) interruptTrackedPetRequestForRetry(now);
    cancelAutonomous();
    clearBlinkForExclusiveReaction();

    Serial.print("SLEEP REQUEST ARMED - STAGE ");
    Serial.println(currentStage);
  }
}

void updateStageByTime(uint32_t now) {
  if (!demoStarted || completionFlag) return;

  uint32_t elapsed = getActiveElapsedMs(now);

  if (elapsed >= TOTAL_DEMO_MS) {
    // Final boundary retains PET/FOOD auto-fill only. A missed SLEEP remains
    // missing so Completion never synthesizes a credit that did not happen.
    autoFillPetAndFoodCreditsForClosingStage();

    completionFlag = true;
    demoClockRunning = false;
    savedActiveMs = TOTAL_DEMO_MS;
    currentStage = 3;
    sleepRequestPending = false;
    sleepCreditWindowAnnouncedStage = 0;

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
    // Close the old Stage before replacing its care mask. Only PET/FOOD may
    // follow the existing boundary auto-fill policy; SLEEP never does.
    autoFillPetAndFoodCreditsForClosingStage();

    currentStage = targetStage;
    stageCareMask = 0;
    sleepRequestPending = false;
    sleepCreditWindowAnnouncedStage = 0;
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

void setupDemoStorage() {
  // ----------------------------------------------------------
  // NVS
  // ----------------------------------------------------------
  if (!prefs.begin("tandoDemo", false)) {
    Serial.println("FATAL: NVS / Preferences open failed");
    while (true) delay(1000);
  }

  loadPersistentState();
}

void restoreDemoRuntime(uint32_t now) {
  lastInteractionMs = now;
  lastNvsCheckpointElapsed = savedActiveMs;

  // Validate Stage against persisted active time after an interrupted power cycle.
  if (!completionFlag) {
    if (savedActiveMs >= 2UL * STAGE_MS) {
      if (currentStage < 3) {
        currentStage = 3;
        stageCareMask = 0;
        sleepRequestPending = false;
        sleepCreditWindowAnnouncedStage = 0;
      }
    } else if (savedActiveMs >= STAGE_MS) {
      if (currentStage < 2) {
        currentStage = 2;
        stageCareMask = 0;
        sleepRequestPending = false;
        sleepCreditWindowAnnouncedStage = 0;
      }
    }
  }
}
