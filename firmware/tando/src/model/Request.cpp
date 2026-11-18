#include "Request.h"
#include "Reaction.h"
#include "Demo.h"
#include "../controller/DemoController.h"

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
  return ((slotIndex + demo.stage) & 0x01) == 0;
}

uint32_t chooseCareRequestTarget(
  uint32_t now,
  bool hungerFamily,
  uint8_t *slotIndex,
  bool retrySoon
) {
  if (demo.completed) return 0;

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


bool Request::needSatisfiedThisStage() {
  return demo.completed || ((demo.stageCareMask & this->requestBit) != 0);
}

void Request::clearPromptRuntime(){
  promptActive = false;
  promptTracked = false;
  promptManualPreview = false;
  promptStart = 0;
}

void Request::scheduleNextRequest(uint32_t now, bool retrySoon) {
  if (demo.completed ||
      needSatisfiedThisStage() ||
      requestsShown >= requestsPerStage ||
      requestSlot >= requestsPerStage
    ) {
      nextRequestAt = 0;
      return;
    }

  nextRequestAt = chooseCareRequestTarget (
    now, true, &requestSlot, retrySoon
  );

  if (nextRequestAt == 0) return;

  uint32_t stageWall = getCurrentCareStageWallMs(now);
  uint32_t delayMs = (nextRequestAt > stageWall) ? (nextRequestAt - stageWall) : 0;

  Serial.print(this->requestName);
  Serial.print(" SCHEDULED: stage=");
  Serial.print(" slot=");
  Serial.print(requestSlot + 1);
  Serial.print("/10 completed=");
  Serial.print(requestsShown);
  Serial.print("/10 in ");
  Serial.print(delayMs / 1000UL);
  Serial.println(" wall-s");
}

//TODO: so the logic i finally decided to go with is that these two cannot intefere with each other.
//now, I know this is dirty code but let's just get over with until I fix it.
void Request::startPrompt(uint32_t now, bool tracked) {
  if (hungerRequest.promptActive) return; //this line is dirty code.
  promptActive = true;
  promptTracked = tracked;
  promptManualPreview = !tracked;
  promptStart = now;
  retryPending = false;

  if (this->requestName == "HUNGER")
    Serial.print(tracked ? "*** HUNGER OVERLAY START " : "*** HUNGER OVERLAY PREVIEW ");
  else if (this->requestName == "PET")
    Serial.print(tracked ? "*** PET REQUEST START " : "*** PET REQUEST PREVIEW ");
  if (tracked) {
    Serial.print(requestsShown + 1);
    Serial.print("/");
    Serial.print(requestsPerStage);
    Serial.print(" ");
  }
  if (this->requestName == "HUNGER")
    Serial.println("- 10 s, eyes remain active ***");
  else if (this->requestName == "PET")
    Serial.println("- 10 s, affectionate eyes + ((heart)) alert ***");
}


void Request::resetRequestForStage(uint32_t now)
{
  clearPromptRuntime();

  requestStage = demo.stage;
  requestsShown = 0;
  requestSlot = 0;
  nextRequestAt = 0;
  resetCareStageWallClock(now);
  retryPending = false;

  if (!demo.completed && !needSatisfiedThisStage()) {
    scheduleNextRequest(now, false);
  }
  
}


void Request::reset() {
  this->requestStage = 0;
  this->requestsShown = 0;
  this->nextRequestAt = 0;
  this->requestSlot = 0;
  this->promptActive = false;
  this->promptTracked = false;
  this->promptManualPreview = false;
  this->promptStart = 0;
  this->retryPending = false;
}

void Request::restore(uint8_t stage, uint8_t shown) {
  reset();
  requestStage = stage;
  requestsShown = shown;
  requestSlot = shown;
}


void Request::syncRequestStage(uint32_t now) {
  if (requestStage != demo.stage) {
    requestStage = demo.stage;
    requestsShown = 0;
  }

  if (requestsShown > requestsPerStage) {
    requestsShown = requestsPerStage;
  }

  requestSlot = requestsShown;
  clearPromptRuntime();
  nextRequestAt = 0;
  retryPending = false;

  if (!demo.completed && !needSatisfiedThisStage()) {
    scheduleNextRequest(now, false);
  }
}


void Request::satisfyNeedForStage(uint32_t now) {
  clearPromptRuntime();
  retryPending = false;
  nextRequestAt = 0;

  Serial.print(requestName);
  Serial.print(" NEED SATISFIED - STAGE ");
  Serial.println(demo.stage);

}


void Request::completeTrackedRequest(uint32_t now) {
  if (!promptActive || !promptTracked) return;

  clearPromptRuntime();
  retryPending = false;

  if (requestsShown < requestsPerStage) {
    requestsShown++;
  }
  if (requestSlot < requestsPerStage) {
    requestSlot++;
  }
  careRequestNextAllowedWallMs = getCurrentCareStageWallMs(now) + CARE_REQUEST_VISUAL_COOLDOWN_MS;

  Serial.print(requestName);
  if (requestName == "HUNGER")
    Serial.print(" OVERLAY");
  else
    Serial.print(" REQUEST");
  Serial.print(" COMPLETE: stage=");
  Serial.print(demo.stage);
  Serial.print(" completed=");
  Serial.print(requestsShown);
  Serial.print("/");
  Serial.println(requestsPerStage);
  
  demo.save(now);

  if (!needSatisfiedThisStage() && requestsShown < requestsPerStage)
    scheduleNextRequest(now, false);
  else
    nextRequestAt = 0;

}


void Request::updateRequestScheduler(uint32_t now) {
  if (requestStage != demo.stage) 
    resetRequestForStage(now);

  // Manual r preview is independent of PET satisfaction, quota and Demo clock.
  // Manual h preview is intentionally independent of Stage satisfaction,
  // automatic quota and Demo clock state. Only a real/queued reaction may
  // dismiss it before its 10-second preview duration completes.
  if (promptActive && promptManualPreview) {
    if (blockedByPriority()) {
      clearPromptRuntime();
      return;
    }
    if ((now - promptStart) >= requestDuration )
      clearPromptRuntime();
    return;
  }

  if (demo.completed || needSatisfiedThisStage()) {
    if (promptActive)
      clearPromptRuntime();
    nextRequestAt = 0;
    retryPending = false;
    return;
  }

  if (promptActive && blockedByPriority()) {
    interruptTrackedRequestPromptForRetry(now);
    return;
  }

  if (!promptActive && blockedByPriority()) {
    uint32_t stageWall = getCurrentCareStageWallMs(now);
    if (nextRequestAt != 0 &&
        (int32_t)(stageWall - nextRequestAt) >= 0) {
      nextRequestAt = 0;
      retryPending = true;
    }
    return;
  }

  if (promptActive) {
    if ((now - promptStart) >= requestDuration) {
      if (promptTracked) {
        // completeTrackedPetRequest(now);
        petRequest.completeTrackedRequest(now);
      } else {
        clearPromptRuntime();
      }
    }
    return;
  }

    if (requestsShown >= requestsPerStage ||
      requestSlot >= requestsPerStage) {
    nextRequestAt = 0;
    return;
  }

  if (promptActive) return;

  if (nextRequestAt == 0) {
    bool retrySoon = retryPending;
    retryPending = false;
    scheduleNextRequest(now, retrySoon);
    return;
  }

  uint32_t stageWall = getCurrentCareStageWallMs(now);
  if (stageWall < careRequestNextAllowedWallMs) return;

  if ((int32_t)(stageWall - nextRequestAt) >= 0) {
    nextRequestAt = 0;
    startPrompt(now, true);
  }
  
}


bool Request::blockedByPriority() {
  // Any real or queued reaction temporarily dismisses the PET Request so its
  // heart alert is never visually mixed with PET/FOOD/SLEEP/Unknown/System.
  return reaction != R_NONE ||
         demo.sleepRequestPending ||
         petReaction.pending ||
         foodReaction.pending != 0 ||
         confusedReaction.pending ||
         sleepReaction.pending ||
         completionReaction.pending ||
         pendingUnlockMask != 0;
}


void Request::interruptTrackedRequestPromptForRetry(uint32_t now) {
  if (!promptActive) return;

  bool wasTracked = promptTracked;
  clearPromptRuntime();

  if (wasTracked &&
      !demo.completed &&
      !needSatisfiedThisStage() &&
      requestsShown < requestsPerStage) {
    retryPending = true;
    nextRequestAt = 0;
  } else {
    retryPending = false;
  }
}

