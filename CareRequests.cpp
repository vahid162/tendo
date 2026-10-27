#include "CareRequests.h"
#include "TandoConfig.h"
#include "Demo.h"
#include "Reactions.h"

// Keeps Hunger and PET quotas, retries, and their shared wall-clock timeline together.

// Local declarations replace Arduino-generated prototypes.
static void resetCareStageWallClock(uint32_t now);
static bool hungerUsesEarlyCareWindow(uint8_t slotIndex);
static uint32_t chooseCareRequestTarget(
  uint32_t now,
  bool hungerFamily,
  uint8_t *slotIndex,
  bool retrySoon
);
static void scheduleNextHungerRequest(uint32_t now, bool retrySoon);
static void completeTrackedHungerRequest(uint32_t now);
static void scheduleNextPetRequest(uint32_t now, bool retrySoon);
static void completeTrackedPetRequest(uint32_t now);

uint8_t hungerRequestStage = 0;
uint8_t hungerRequestsShown = 0;
uint32_t nextHungerRequestAt = 0;
uint8_t hungerRequestSlot = 0;
bool hungerPromptActive = false;
bool hungerPromptTracked = false;
bool hungerPromptManualPreview = false;
uint32_t hungerPromptStart = 0;
bool hungerRetryPending = false;
uint8_t petRequestStage = 0;
uint8_t petRequestsShown = 0;
uint32_t nextPetRequestAt = 0;
uint8_t petRequestSlot = 0;
bool petRequestPromptActive = false;
bool petRequestPromptTracked = false;
bool petRequestPromptManualPreview = false;
uint32_t petRequestPromptStart = 0;
bool petRequestRetryPending = false;
uint32_t careStageWallStartMs = 0;
bool careStageWallClockReady = false;
uint32_t careRequestNextAllowedWallMs = 0;

void resetCareStageWallClock(uint32_t now) {
  careStageWallStartMs = now;
  careStageWallClockReady = true;
  careRequestNextAllowedWallMs = 0;
}

uint32_t getCurrentCareStageWallMs(uint32_t now) {
  if (!careStageWallClockReady) {
    resetCareStageWallClock(now);
  }
  return now - careStageWallStartMs;
}

bool hungerUsesEarlyCareWindow(uint8_t slotIndex) {
  return ((slotIndex + currentStage) & 0x01) == 0;
}

uint32_t chooseCareRequestTarget(
  uint32_t now,
  bool hungerFamily,
  uint8_t *slotIndex,
  bool retrySoon
) {
  if (completionFlag) return 0;

  uint32_t stageWall = getCurrentCareStageWallMs(now);

  if (retrySoon) {
    return stageWall + (uint32_t)random(
      (long)CARE_REQUEST_RETRY_GAP_MIN_MS,
      (long)CARE_REQUEST_RETRY_GAP_MAX_MS + 1L
    );
  }

  while (*slotIndex < HUNGER_REQUESTS_PER_STAGE) {
    bool hungerEarly = hungerUsesEarlyCareWindow(*slotIndex);
    bool useEarly = hungerFamily ? hungerEarly : !hungerEarly;

    uint32_t base = (uint32_t)(*slotIndex) * CARE_REQUEST_SLOT_MS;
    uint32_t windowMin = base + (useEarly ? CARE_REQUEST_EARLY_MIN_MS : CARE_REQUEST_LATE_MIN_MS);
    uint32_t windowMax = base + (useEarly ? CARE_REQUEST_EARLY_MAX_MS : CARE_REQUEST_LATE_MAX_MS);

    // If a long reaction/sleep/reboot made a natural slot stale, skip it
    // rather than replaying old requests in a burst.
    if (stageWall + 1000UL > windowMax) {
      (*slotIndex)++;
      continue;
    }

    uint32_t lower = windowMin;
    if (lower < stageWall + 1000UL) lower = stageWall + 1000UL;

    return (uint32_t)random((long)lower, (long)windowMax + 1L);
  }

  return 0;
}

bool hungerNeedSatisfiedThisStage() {
  return completionFlag || ((stageCareMask & CARE_FOOD_BIT) != 0);
}

bool hungerOverlayBlockedByPriority() {
  return reaction != R_NONE ||
         sleepRequestPending ||
         pendingPetVisual ||
         pendingFoodVisual != 0 ||
         pendingConfusedVisual ||
         pendingSleepVisual ||
         pendingCompletion ||
         pendingUnlockMask != 0;
}

void clearHungerPromptRuntime() {
  hungerPromptActive = false;
  hungerPromptTracked = false;
  hungerPromptManualPreview = false;
  hungerPromptStart = 0;
}

void scheduleNextHungerRequest(uint32_t now, bool retrySoon) {
  if (completionFlag ||
      hungerNeedSatisfiedThisStage() ||
      hungerRequestsShown >= HUNGER_REQUESTS_PER_STAGE ||
      hungerRequestSlot >= HUNGER_REQUESTS_PER_STAGE) {
    nextHungerRequestAt = 0;
    return;
  }

  nextHungerRequestAt = chooseCareRequestTarget(
    now,
    true,
    &hungerRequestSlot,
    retrySoon
  );

  if (nextHungerRequestAt == 0) return;

  uint32_t stageWall = getCurrentCareStageWallMs(now);
  uint32_t delayMs = (nextHungerRequestAt > stageWall)
    ? (nextHungerRequestAt - stageWall)
    : 0;

  Serial.print("HUNGER SCHEDULED: stage=");
  Serial.print(currentStage);
  Serial.print(" slot=");
  Serial.print(hungerRequestSlot + 1);
  Serial.print("/10 completed=");
  Serial.print(hungerRequestsShown);
  Serial.print("/10 in ");
  Serial.print(delayMs / 1000UL);
  Serial.println(" wall-s");
}

void startHungerPrompt(uint32_t now, bool tracked) {
  hungerPromptActive = true;
  hungerPromptTracked = tracked;
  hungerPromptManualPreview = !tracked;
  hungerPromptStart = now;
  hungerRetryPending = false;

  Serial.print(tracked ? "*** HUNGER OVERLAY START " : "*** HUNGER OVERLAY PREVIEW ");
  if (tracked) {
    Serial.print(hungerRequestsShown + 1);
    Serial.print("/");
    Serial.print(HUNGER_REQUESTS_PER_STAGE);
    Serial.print(" ");
  }
  Serial.println("- 10 s, eyes remain active ***");
}

void interruptTrackedHungerPromptForRetry(uint32_t now) {
  if (!hungerPromptActive) return;

  bool wasTracked = hungerPromptTracked;
  clearHungerPromptRuntime();

  if (wasTracked &&
      !completionFlag &&
      !hungerNeedSatisfiedThisStage() &&
      hungerRequestsShown < HUNGER_REQUESTS_PER_STAGE) {
    hungerRetryPending = true;
    nextHungerRequestAt = 0;
  } else {
    hungerRetryPending = false;
  }
}

void resetHungerRequestForStage(uint32_t now) {
  clearHungerPromptRuntime();

  hungerRequestStage = currentStage;
  hungerRequestsShown = 0;
  hungerRequestSlot = 0;
  nextHungerRequestAt = 0;
  resetCareStageWallClock(now);
  hungerRetryPending = false;

  if (!completionFlag && !hungerNeedSatisfiedThisStage()) {
    scheduleNextHungerRequest(now, false);
  }
}

void syncHungerRequestStage(uint32_t now) {
  if (hungerRequestStage != currentStage) {
    hungerRequestStage = currentStage;
    hungerRequestsShown = 0;
  }

  if (hungerRequestsShown > HUNGER_REQUESTS_PER_STAGE) {
    hungerRequestsShown = HUNGER_REQUESTS_PER_STAGE;
  }

  hungerRequestSlot = hungerRequestsShown;
  clearHungerPromptRuntime();
  nextHungerRequestAt = 0;
  hungerRetryPending = false;

  if (!completionFlag && !hungerNeedSatisfiedThisStage()) {
    scheduleNextHungerRequest(now, false);
  }
}

void satisfyHungerNeedForStage(uint32_t now) {
  clearHungerPromptRuntime();
  hungerRetryPending = false;
  nextHungerRequestAt = 0;

  Serial.print("HUNGER NEED SATISFIED - STAGE ");
  Serial.println(currentStage);
}

void completeTrackedHungerRequest(uint32_t now) {
  if (!hungerPromptActive || !hungerPromptTracked) return;

  clearHungerPromptRuntime();
  hungerRetryPending = false;

  if (hungerRequestsShown < HUNGER_REQUESTS_PER_STAGE) {
    hungerRequestsShown++;
  }
  if (hungerRequestSlot < HUNGER_REQUESTS_PER_STAGE) {
    hungerRequestSlot++;
  }
  careRequestNextAllowedWallMs =
    getCurrentCareStageWallMs(now) + CARE_REQUEST_VISUAL_COOLDOWN_MS;

  Serial.print("HUNGER OVERLAY COMPLETE: stage=");
  Serial.print(currentStage);
  Serial.print(" completed=");
  Serial.print(hungerRequestsShown);
  Serial.print("/");
  Serial.println(HUNGER_REQUESTS_PER_STAGE);

  savePersistentState(now);

  if (!hungerNeedSatisfiedThisStage() &&
      hungerRequestsShown < HUNGER_REQUESTS_PER_STAGE) {
    scheduleNextHungerRequest(now, false);
  } else {
    nextHungerRequestAt = 0;
  }
}

void updateHungerRequestScheduler(uint32_t now) {
  if (hungerRequestStage != currentStage) {
    resetHungerRequestForStage(now);
  }

  // Manual h preview is intentionally independent of Stage satisfaction,
  // automatic quota and Demo clock state. Only a real/queued reaction may
  // dismiss it before its 10-second preview duration completes.
  if (hungerPromptActive && hungerPromptManualPreview) {
    if (hungerOverlayBlockedByPriority()) {
      clearHungerPromptRuntime();
      return;
    }
    if ((now - hungerPromptStart) >= HUNGER_REQUEST_DURATION_MS) {
      clearHungerPromptRuntime();
    }
    return;
  }

  if (completionFlag || hungerNeedSatisfiedThisStage()) {
    if (hungerPromptActive) clearHungerPromptRuntime();
    nextHungerRequestAt = 0;
    hungerRetryPending = false;
    return;
  }

  if (hungerPromptActive && hungerOverlayBlockedByPriority()) {
    interruptTrackedHungerPromptForRetry(now);
    return;
  }

  // Do not start/schedule while a real/queued reaction owns the screen.
  // If a natural target became due during that reaction, convert it to a
  // post-reaction retry instead of firing immediately on the first free frame.
  if (!hungerPromptActive && hungerOverlayBlockedByPriority()) {
    uint32_t stageWall = getCurrentCareStageWallMs(now);
    if (nextHungerRequestAt != 0 &&
        (int32_t)(stageWall - nextHungerRequestAt) >= 0) {
      nextHungerRequestAt = 0;
      hungerRetryPending = true;
    }
    return;
  }

  if (hungerPromptActive) {
    if ((now - hungerPromptStart) >= HUNGER_REQUEST_DURATION_MS) {
      if (hungerPromptTracked) {
        completeTrackedHungerRequest(now);
      } else {
        clearHungerPromptRuntime();
      }
    }
    return;
  }

  if (hungerRequestsShown >= HUNGER_REQUESTS_PER_STAGE ||
      hungerRequestSlot >= HUNGER_REQUESTS_PER_STAGE) {
    nextHungerRequestAt = 0;
    return;
  }

  if (petRequestPromptActive) return;

  if (nextHungerRequestAt == 0) {
    bool retrySoon = hungerRetryPending;
    hungerRetryPending = false;
    scheduleNextHungerRequest(now, retrySoon);
    return;
  }

  uint32_t stageWall = getCurrentCareStageWallMs(now);
  if (stageWall < careRequestNextAllowedWallMs) return;

  if ((int32_t)(stageWall - nextHungerRequestAt) >= 0) {
    bool petDueNow =
      nextPetRequestAt != 0 &&
      (int32_t)(stageWall - nextPetRequestAt) >= 0 &&
      !completionFlag &&
      !petRequestNeedSatisfiedThisStage() &&
      petRequestsShown < PET_REQUESTS_PER_STAGE &&
      petRequestSlot < PET_REQUESTS_PER_STAGE;

    if (petDueNow && random(2) == 0) {
      uint32_t deferMs = (uint32_t)random(
        (long)CARE_REQUEST_COLLISION_DEFER_MIN_MS,
        (long)CARE_REQUEST_COLLISION_DEFER_MAX_MS + 1L
      );
      nextHungerRequestAt = stageWall + deferMs;
      Serial.println("CARE REQUEST COLLISION -> PET REQUEST GETS THIS SLOT");
      return;
    }

    nextHungerRequestAt = 0;
    startHungerPrompt(now, true);
  }
}

bool petRequestNeedSatisfiedThisStage() {
  return completionFlag || ((stageCareMask & CARE_PET_BIT) != 0);
}

bool petRequestBlockedByPriority() {
  // Any real or queued reaction temporarily dismisses the PET Request so its
  // heart alert is never visually mixed with PET/FOOD/SLEEP/Unknown/System.
  return reaction != R_NONE ||
         sleepRequestPending ||
         pendingPetVisual ||
         pendingFoodVisual != 0 ||
         pendingConfusedVisual ||
         pendingSleepVisual ||
         pendingCompletion ||
         pendingUnlockMask != 0;
}

void clearPetRequestPromptRuntime() {
  petRequestPromptActive = false;
  petRequestPromptTracked = false;
  petRequestPromptManualPreview = false;
  petRequestPromptStart = 0;
}

void scheduleNextPetRequest(uint32_t now, bool retrySoon) {
  if (completionFlag ||
      petRequestNeedSatisfiedThisStage() ||
      petRequestsShown >= PET_REQUESTS_PER_STAGE ||
      petRequestSlot >= PET_REQUESTS_PER_STAGE) {
    nextPetRequestAt = 0;
    return;
  }

  nextPetRequestAt = chooseCareRequestTarget(
    now,
    false,
    &petRequestSlot,
    retrySoon
  );

  if (nextPetRequestAt == 0) return;

  uint32_t stageWall = getCurrentCareStageWallMs(now);
  uint32_t delayMs = (nextPetRequestAt > stageWall)
    ? (nextPetRequestAt - stageWall)
    : 0;

  Serial.print("PET REQUEST SCHEDULED: stage=");
  Serial.print(currentStage);
  Serial.print(" slot=");
  Serial.print(petRequestSlot + 1);
  Serial.print("/10 completed=");
  Serial.print(petRequestsShown);
  Serial.print("/10 in ");
  Serial.print(delayMs / 1000UL);
  Serial.println(" wall-s");
}

void startPetRequestPrompt(uint32_t now, bool tracked) {
  // Care prompts must remain visually singular.
  if (hungerPromptActive) return;

  petRequestPromptActive = true;
  petRequestPromptTracked = tracked;
  petRequestPromptManualPreview = !tracked;
  petRequestPromptStart = now;
  petRequestRetryPending = false;

  Serial.print(tracked ? "*** PET REQUEST START " : "*** PET REQUEST PREVIEW ");
  if (tracked) {
    Serial.print(petRequestsShown + 1);
    Serial.print("/");
    Serial.print(PET_REQUESTS_PER_STAGE);
    Serial.print(" ");
  }
  Serial.println("- 10 s, affectionate eyes + ((heart)) alert ***");
}

void interruptTrackedPetRequestForRetry(uint32_t now) {
  if (!petRequestPromptActive) return;

  bool wasTracked = petRequestPromptTracked;
  clearPetRequestPromptRuntime();

  if (wasTracked &&
      !completionFlag &&
      !petRequestNeedSatisfiedThisStage() &&
      petRequestsShown < PET_REQUESTS_PER_STAGE) {
    petRequestRetryPending = true;
    nextPetRequestAt = 0;
  } else {
    petRequestRetryPending = false;
  }
}

void resetPetRequestForStage(uint32_t now) {
  clearPetRequestPromptRuntime();

  petRequestStage = currentStage;
  petRequestsShown = 0;
  petRequestSlot = 0;
  nextPetRequestAt = 0;
  resetCareStageWallClock(now);
  petRequestRetryPending = false;

  if (!completionFlag && !petRequestNeedSatisfiedThisStage()) {
    scheduleNextPetRequest(now, false);
  }
}

void syncPetRequestStage(uint32_t now) {
  if (petRequestStage != currentStage) {
    petRequestStage = currentStage;
    petRequestsShown = 0;
  }

  if (petRequestsShown > PET_REQUESTS_PER_STAGE) {
    petRequestsShown = PET_REQUESTS_PER_STAGE;
  }

  petRequestSlot = petRequestsShown;
  clearPetRequestPromptRuntime();
  nextPetRequestAt = 0;
  petRequestRetryPending = false;

  if (!completionFlag && !petRequestNeedSatisfiedThisStage()) {
    scheduleNextPetRequest(now, false);
  }
}

void satisfyPetRequestNeedForStage(uint32_t now) {
  clearPetRequestPromptRuntime();
  petRequestRetryPending = false;
  nextPetRequestAt = 0;

  Serial.print("PET REQUEST NEED SATISFIED - STAGE ");
  Serial.println(currentStage);
}

void completeTrackedPetRequest(uint32_t now) {
  if (!petRequestPromptActive || !petRequestPromptTracked) return;

  clearPetRequestPromptRuntime();
  petRequestRetryPending = false;

  if (petRequestsShown < PET_REQUESTS_PER_STAGE) {
    petRequestsShown++;
  }
  if (petRequestSlot < PET_REQUESTS_PER_STAGE) {
    petRequestSlot++;
  }
  careRequestNextAllowedWallMs =
    getCurrentCareStageWallMs(now) + CARE_REQUEST_VISUAL_COOLDOWN_MS;

  Serial.print("PET REQUEST COMPLETE: stage=");
  Serial.print(currentStage);
  Serial.print(" completed=");
  Serial.print(petRequestsShown);
  Serial.print("/");
  Serial.println(PET_REQUESTS_PER_STAGE);

  savePersistentState(now);

  if (!petRequestNeedSatisfiedThisStage() &&
      petRequestsShown < PET_REQUESTS_PER_STAGE) {
    scheduleNextPetRequest(now, false);
  } else {
    nextPetRequestAt = 0;
  }
}

void updatePetRequestScheduler(uint32_t now) {
  if (petRequestStage != currentStage) {
    resetPetRequestForStage(now);
  }

  // Manual r preview is independent of PET satisfaction, quota and Demo clock.
  if (petRequestPromptActive && petRequestPromptManualPreview) {
    if (petRequestBlockedByPriority()) {
      clearPetRequestPromptRuntime();
      return;
    }
    if ((now - petRequestPromptStart) >= PET_REQUEST_DURATION_MS) {
      clearPetRequestPromptRuntime();
    }
    return;
  }

  if (completionFlag || petRequestNeedSatisfiedThisStage()) {
    if (petRequestPromptActive) clearPetRequestPromptRuntime();
    nextPetRequestAt = 0;
    petRequestRetryPending = false;
    return;
  }

  if (petRequestPromptActive && petRequestBlockedByPriority()) {
    interruptTrackedPetRequestForRetry(now);
    return;
  }

  if (!petRequestPromptActive && petRequestBlockedByPriority()) {
    uint32_t stageWall = getCurrentCareStageWallMs(now);
    if (nextPetRequestAt != 0 &&
        (int32_t)(stageWall - nextPetRequestAt) >= 0) {
      nextPetRequestAt = 0;
      petRequestRetryPending = true;
    }
    return;
  }

  if (petRequestPromptActive) {
    if ((now - petRequestPromptStart) >= PET_REQUEST_DURATION_MS) {
      if (petRequestPromptTracked) {
        completeTrackedPetRequest(now);
      } else {
        clearPetRequestPromptRuntime();
      }
    }
    return;
  }

  if (petRequestsShown >= PET_REQUESTS_PER_STAGE ||
      petRequestSlot >= PET_REQUESTS_PER_STAGE) {
    nextPetRequestAt = 0;
    return;
  }

  if (hungerPromptActive) return;

  if (nextPetRequestAt == 0) {
    bool retrySoon = petRequestRetryPending;
    petRequestRetryPending = false;
    scheduleNextPetRequest(now, retrySoon);
    return;
  }

  uint32_t stageWall = getCurrentCareStageWallMs(now);
  if (stageWall < careRequestNextAllowedWallMs) return;

  if ((int32_t)(stageWall - nextPetRequestAt) >= 0) {
    nextPetRequestAt = 0;
    startPetRequestPrompt(now, true);
  }
}
