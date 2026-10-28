#include "Reactions.h"
#include "TandoConfig.h"
#include "Demo.h"
#include "CareRequests.h"
#include "Eyes.h"
#include <esp_arduino_version.h>
#include <math.h>

// Owns exclusive real/system reactions, pending visuals, Sleep, and LED feedback.

// Local declarations replace Arduino-generated prototypes.
static void setReactionLedDuty(uint8_t duty);
static void triggerLedPulse(uint8_t peak, uint8_t repeats, uint32_t now);
static void beginReactionSettle(uint32_t now);
static bool reactionIsSystemPriority();
static void clearPendingUserVisuals();
static void startPetReaction(uint32_t now);
static void startFoodReaction(uint32_t now);
static void startSleepReaction(uint32_t now);
static void startConfusedReaction(uint32_t now);
static void startUnlockReaction(uint8_t stageNumber, uint32_t now);
static void startCompletionReaction(uint32_t now);
static void finishReaction(uint32_t now);

uint8_t pendingUnlockMask = 0;
bool pendingCompletion = false;
static bool ledPulseActive = false;
static uint32_t ledPulseStart = 0;
static uint8_t ledPulsePeak = LED_REACTION_PEAK;
static uint8_t ledPulseRepeats = 1;

void setReactionLedDuty(uint8_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(REACTION_LED_PIN, duty);
#else
  ledcWrite(LED_PWM_OLD_CHANNEL, duty);
#endif
}

void setupReactionLed() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  bool ok = ledcAttach(REACTION_LED_PIN, LED_PWM_FREQ, LED_PWM_RES_BITS);
  if (!ok) {
    Serial.println("WARNING: LED PWM attach failed");
  }
#else
  ledcSetup(LED_PWM_OLD_CHANNEL, LED_PWM_FREQ, LED_PWM_RES_BITS);
  ledcAttachPin(REACTION_LED_PIN, LED_PWM_OLD_CHANNEL);
#endif

  setReactionLedDuty(LED_BASE_DUTY);
}

void triggerLedPulse(uint8_t peak, uint8_t repeats, uint32_t now) {
  if (peak < LED_BASE_DUTY) peak = LED_BASE_DUTY;
  if (repeats < 1) repeats = 1;

  ledPulsePeak = peak;
  ledPulseRepeats = repeats;
  ledPulseStart = now;
  ledPulseActive = true;
}

void updateReactionLed(uint32_t now) {
  uint8_t duty = LED_BASE_DUTY;

  if (ledPulseActive) {
    uint32_t elapsed = now - ledPulseStart;
    uint32_t total = (uint32_t)ledPulseRepeats * LED_CYCLE_MS;

    if (elapsed >= total) {
      ledPulseActive = false;
      duty = LED_BASE_DUTY;
    } else {
      uint32_t phase = elapsed % LED_CYCLE_MS;
      float level = 0.0f;

      if (phase < LED_ATTACK_MS) {
        level = smoothStep((float)phase / (float)LED_ATTACK_MS);
      } else if (phase < LED_ATTACK_MS + LED_HOLD_MS) {
        level = 1.0f;
      } else if (phase < LED_ATTACK_MS + LED_HOLD_MS + LED_RELEASE_MS) {
        uint32_t releasePhase = phase - LED_ATTACK_MS - LED_HOLD_MS;
        level = 1.0f - smoothStep((float)releasePhase / (float)LED_RELEASE_MS);
      } else {
        level = 0.0f;
      }

      float out = (float)LED_BASE_DUTY + ((float)ledPulsePeak - (float)LED_BASE_DUTY) * level;
      if (out < 0.0f) out = 0.0f;
      if (out > 255.0f) out = 255.0f;
      duty = (uint8_t)out;
    }
  }

  setReactionLedDuty(duty);
}

bool sleepTagPresent = false;
uint32_t sleepWakeStart = 0;
Reaction reaction = R_NONE;
uint32_t reactionStart = 0;
static bool foodBlinkTriggered = false;
bool pendingPetVisual = false;
uint8_t pendingFoodVisual = 0;
bool pendingConfusedVisual = false;
bool pendingSleepVisual = false;
static uint32_t userReactionReadyAt = 0;

bool userReactionCanStart(uint32_t now) {
  return (int32_t)(now - userReactionReadyAt) >= 0;
}

void beginReactionSettle(uint32_t now) {
  userReactionReadyAt = now + REACTION_SETTLE_MS;
  blinkState = BLINK_OPEN;
  blinkAmount = 0.0f;
  scheduleNextBlink(now);
}

bool reactionIsBusy() {
  return reaction != R_NONE;
}

bool reactionIsSystemPriority() {
  return reaction == R_UNLOCK2 ||
         reaction == R_UNLOCK3 ||
         reaction == R_COMPLETE;
}

void clearBlinkForExclusiveReaction() {
  blinkState = BLINK_OPEN;
  blinkAmount = 0.0f;
  blinkStart = 0;
}

void clearPendingUserVisuals() {
  pendingPetVisual = false;
  pendingFoodVisual = 0;
  pendingConfusedVisual = false;
  pendingSleepVisual = false;
}

void startPetReaction(uint32_t now) {
  // A second valid PET is allowed to restart/refresh the PET animation.
  // Other atomic reactions still keep their priority and are not interrupted.
  if (reactionIsBusy() && reaction != R_PET) return;

  reaction = R_PET;
  reactionStart = now;

  // PET should read immediately as a deliberate affectionate look.
  // Cancel an incidental idle blink so the first part of the reaction
  // is not hidden by a blink that happened to start at the same moment.
  blinkState = BLINK_OPEN;
  blinkAmount = 0.0f;
  blinkStart = 0;
  nextBlink = now + 5600UL + (uint32_t)random(0, 1800);

  Serial.println("PET REACTION - CLEAR AFFECTIONATE UP LOOK");
}

void startFoodReaction(uint32_t now) {
  if (reactionIsBusy()) return;
  clearBlinkForExclusiveReaction();
  reaction = R_FOOD;
  reactionStart = now;
  foodBlinkTriggered = false;
  Serial.println("FOOD REACTION");
}

void startSleepReaction(uint32_t now) {
  if (reactionIsBusy()) return;

  clearBlinkForExclusiveReaction();
  reaction = R_SLEEP;
  reactionStart = now;
  sleepTagPresent = true;
  sleepWakeStart = 0;

  Serial.println("SLEEP STATE ENTERED - WAITING FOR TAG REMOVAL");
}

void beginSleepWake(uint32_t now) {
  if (reaction != R_SLEEP) return;
  if (!sleepTagPresent && sleepWakeStart != 0) return;

  sleepTagPresent = false;
  sleepWakeStart = now;
  Serial.println("SLEEP TAG REMOVED -> WAKE UP");
}

void startConfusedReaction(uint32_t now) {
  if (reactionIsBusy()) return;
  clearBlinkForExclusiveReaction();
  reaction = R_CONFUSED;
  reactionStart = now;
  Serial.println("UNKNOWN RFID -> FRIENDLY CONFUSED REACTION");
}

void startUnlockReaction(uint8_t stageNumber, uint32_t now) {
  if (reactionIsBusy()) return;

  cancelAutonomous();
  clearBlinkForExclusiveReaction();

  if (stageNumber == 2) {
    reaction = R_UNLOCK2;
    reactionStart = now;
    triggerLedPulse(LED_STAGE_PEAK, 2, now);
    Serial.println("*** STAGE 2 UNLOCK ***");
  } else if (stageNumber == 3) {
    reaction = R_UNLOCK3;
    reactionStart = now;
    triggerLedPulse(LED_STAGE_PEAK, 2, now);
    Serial.println("*** STAGE 3 UNLOCK ***");
  }
}

void startCompletionReaction(uint32_t now) {
  if (reactionIsBusy()) return;

  cancelAutonomous();
  clearBlinkForExclusiveReaction();
  reaction = R_COMPLETE;
  reactionStart = now;
  triggerLedPulse(LED_COMPLETE_PEAK, 3, now);
  Serial.println();
  Serial.println("====================================");
  Serial.println("TANDO DEMO COMPLETE - PROGRESS LOCKED");
  printProgressState();
  Serial.println("Progress is now locked. Reactions remain active.");
  Serial.println("====================================");
}

void handlePetEvent(uint32_t now) {
  // PET is intentionally disabled while sleeping. This prevents capacitive
  // noise or a hand resting on the enclosure from earning hidden PET credit.
  if (reaction == R_SLEEP || pendingSleepVisual) {
    Serial.println("PET IGNORED - SLEEP ACTIVE");
    return;
  }

  // Real PET feedback owns the screen immediately. If Hunger was requesting
  // food, hide it now and retry only after the PET reaction if still needed.
  if (hungerPromptActive) {
    interruptTrackedHungerPromptForRetry(now);
  }
  satisfyPetRequestNeedForStage(now);
  interruptAutonomousForInteraction(now);
  notifyUserInteraction(now);
  triggerLedPulse(LED_REACTION_PEAK, 1, now);
  registerCareCredit(CARE_PET_BIT, "PET", now);

  if ((reaction == R_NONE && userReactionCanStart(now)) || reaction == R_PET) {
    // Repeated valid petting refreshes the affectionate pose immediately.
    startPetReaction(now);
  } else {
    pendingPetVisual = true;
  }
}

void handleFoodEvent(uint8_t foodNumber, uint32_t now) {
  // A physical SLEEP tag owns the interaction state until it is removed.
  if (reaction == R_SLEEP || pendingSleepVisual) {
    Serial.println("FOOD IGNORED - SLEEP ACTIVE");
    return;
  }

  // Real FOOD feedback owns the screen immediately. If PET Request was
  // visible, hide it now and retry after the FOOD reaction if still needed.
  if (petRequestPromptActive) {
    interruptTrackedPetRequestForRetry(now);
  }
  satisfyHungerNeedForStage(now);
  interruptAutonomousForInteraction(now);
  notifyUserInteraction(now);
  triggerLedPulse(LED_REACTION_PEAK, 1, now);

  if (foodNumber == 1) {
    Serial.println("FOOD TAG 1 ACCEPTED");
  } else {
    Serial.println("FOOD TAG 2 ACCEPTED");
  }

  registerCareCredit(CARE_FOOD_BIT, "FOOD", now);

  if (!reactionIsBusy() && userReactionCanStart(now)) {
    startFoodReaction(now);
  } else {
    // Both food tags use the same visual, but keep the latest tag number for logs.
    pendingFoodVisual = foodNumber;
  }
}

void handleSleepEvent(uint32_t now) {
  if (hungerPromptActive) interruptTrackedHungerPromptForRetry(now);
  if (petRequestPromptActive) interruptTrackedPetRequestForRetry(now);
  interruptAutonomousForInteraction(now);
  notifyUserInteraction(now);
  triggerLedPulse(LED_REACTION_PEAK, 1, now);
  grantSleepCreditForAcceptedTag(now);

  // The physical SLEEP tag is persistent. Once R_SLEEP starts, no other
  // user/system reaction may visually preempt it. A system reaction already
  // in progress may finish before Sleep begins; later system events queue.
  sleepTagPresent = true;
  pendingPetVisual = false;
  pendingFoodVisual = 0;
  pendingConfusedVisual = false;

  if (reactionIsSystemPriority()) {
    pendingSleepVisual = true;
    return;
  }

  pendingSleepVisual = false;

  if (reaction != R_SLEEP) {
    if (reactionIsBusy()) {
      // SLEEP still preempts ordinary user reactions, but allow a short
      // neutral visual handoff so the previous expression does not leak in.
      reaction = R_NONE;
      foodBlinkTriggered = false;
      beginReactionSettle(now);
    }

    if (userReactionCanStart(now)) {
      startSleepReaction(now);
    } else {
      pendingSleepVisual = true;
    }
  }
}

void handleUnknownRfidEvent(uint32_t now) {
  if (reaction == R_SLEEP || pendingSleepVisual) return;

  if (hungerPromptActive) interruptTrackedHungerPromptForRetry(now);
  if (petRequestPromptActive) interruptTrackedPetRequestForRetry(now);
  interruptAutonomousForInteraction(now);
  notifyUserInteraction(now);
  triggerLedPulse(LED_REACTION_PEAK, 1, now);

  if (!reactionIsBusy() && userReactionCanStart(now)) {
    startConfusedReaction(now);
  } else {
    pendingConfusedVisual = true;
  }
}

void servicePendingSystemReaction(uint32_t now) {
  // Persistent Sleep is an absolute visual lock. Stage Unlock/Completion stay
  // queued and are shown only after the SLEEP tag is removed and wake finishes.
  if (reaction == R_SLEEP) return;

  if (reactionIsBusy()) return;

  if (pendingCompletion) {
    pendingCompletion = false;
    pendingUnlockMask = 0;
    startCompletionReaction(now);
    return;
  }

  // Preserve both unlock events if a long SLEEP state spans multiple time gates.
  if (pendingUnlockMask & PENDING_UNLOCK_STAGE2) {
    pendingUnlockMask &= ~PENDING_UNLOCK_STAGE2;
    startUnlockReaction(2, now);
    return;
  }

  if (pendingUnlockMask & PENDING_UNLOCK_STAGE3) {
    pendingUnlockMask &= ~PENDING_UNLOCK_STAGE3;
    startUnlockReaction(3, now);
  }
}

void servicePendingUserReaction(uint32_t now) {
  if (reactionIsBusy()) return;
  if (!userReactionCanStart(now)) return;

  // SLEEP is the highest-priority user state, but only start it while the
  // physical tag is still considered present.
  if (pendingSleepVisual) {
    pendingSleepVisual = false;
    if (sleepTagPresent) {
      startSleepReaction(now);
      return;
    }
  }

  // RFID feedback is explicit, then PET, then friendly unknown-card feedback.
  if (pendingFoodVisual != 0) {
    pendingFoodVisual = 0;
    startFoodReaction(now);
    return;
  }

  if (pendingPetVisual) {
    pendingPetVisual = false;
    startPetReaction(now);
    return;
  }

  if (pendingConfusedVisual) {
    pendingConfusedVisual = false;
    startConfusedReaction(now);
  }
}

void finishReaction(uint32_t now) {
  if (reaction == R_SLEEP) {
    sleepTagPresent = false;
    sleepWakeStart = 0;
  }

  reaction = R_NONE;
  foodBlinkTriggered = false;

  // Prevent expired visual timers from firing immediately after Wake or
  // another long reaction. Both blink and autonomous personality get a fresh,
  // unpredictable delay after every user/system reaction.
  beginReactionSettle(now);
  scheduleNextAutonomous(now);
}

void updateReaction(uint32_t now) {
  if (reaction == R_NONE) {
      return;
  }

  uint32_t elapsed = now - reactionStart;

  switch (reaction) {
    case R_PET:
      // Long enough to be unmistakable, but still quick enough for repeated play.
      if (elapsed >= 4800) finishReaction(now);
      break;

    case R_FOOD:
      if (!foodBlinkTriggered && elapsed >= 450) {
        startBlink(now);
        foodBlinkTriggered = true;
      }
      if (elapsed >= 3350) finishReaction(now);
      break;

    case R_SLEEP:
      // Persistent state: never auto-finish while the SLEEP tag is present.
      if (!sleepTagPresent) {
        if (sleepWakeStart == 0) {
          sleepWakeStart = now;
        }

        if ((now - sleepWakeStart) >= SLEEP_WAKE_MS) {
          finishReaction(now);
          chooseIdleLook(now);
        }
      }
      break;

    case R_CONFUSED:
      if (elapsed >= 1400) finishReaction(now);
      break;

    case R_UNLOCK2:
      if (elapsed >= 1800) finishReaction(now);
      break;

    case R_UNLOCK3:
      if (elapsed >= 2200) finishReaction(now);
      break;

    case R_COMPLETE:
      if (elapsed >= 4300) finishReaction(now);
      break;

    default:
      finishReaction(now);
      break;
  }
}
