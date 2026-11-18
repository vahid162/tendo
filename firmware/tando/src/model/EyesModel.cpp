#include "EyesModel.h"
#include "../TandoConfig.h"
#include "Demo.h"

#include "Reaction.h"
#include "Request.h"
#include <math.h>
#include "../controller/DemoController.h"



EyesModel eyesModel;

float EyesModel::clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

float smoothStep(float t) {
  t = EyesModel::clamp01(t);
  return t * t * (3.0f - 2.0f * t);
}

float EyesModel::smoothFollow(float current, float target, float dt, float speed) {
  float alpha = 1.0f - expf(-speed * dt);
  return current + (target - current) * alpha;
}

float EyesModel::mapF(float value, float inMin, float inMax, float outMin, float outMax) {
  if (inMax == inMin) return outMin;
  float t = (value - inMin) / (inMax - inMin);
  return outMin + (outMax - outMin) * t;
}


void EyesModel::triggerRingPulse(uint32_t now) {
  ringPulseStart = now;
}


void EyesModel::scheduleNextBlink(uint32_t now) {
  nextBlink = now + random(2600, 5600);
}

void EyesModel::startBlink(uint32_t now) {
  if (blinkState != BLINK_OPEN) return;
  blinkState = BLINK_CLOSING;
  blinkStart = now;
}

void EyesModel::chooseIdleLook(uint32_t now) {
  // More playful idle: larger gaze range and more frequent side-to-side looks.
  // Horizontal and diagonal choices are intentionally more common than center.
  int choice = random(12);

  switch (choice) {
    case 0:  idleX = -34; idleY =   0; break;
    case 1:  idleX = +34; idleY =   0; break;
    case 2:  idleX = -32; idleY = -15; break;
    case 3:  idleX = +32; idleY = -15; break;
    case 4:  idleX = -29; idleY = +12; break;
    case 5:  idleX = +29; idleY = +12; break;
    case 6:  idleX = -24; idleY = -19; break;
    case 7:  idleX = +24; idleY = -19; break;
    case 8:  idleX = -34; idleY =  -4; break;
    case 9:  idleX = +34; idleY =  -4; break;
    case 10: idleX =   0; idleY = -19; break;
    default: idleX =   0; idleY =   0; break;
  }

  long minDelay = 850;
  long maxDelay = 1900;

  if (demo.stage == 2) {
    minDelay = 850;
    maxDelay = 1750;
  } else if (demo.stage == 3) {
    minDelay = 750;
    maxDelay = 1600;
  }

  nextIdleLook = now + random(minDelay, maxDelay);
}


const char* AutonomousState:: getName(AutonomousStateType type) {
    switch (type) {
        case AUTO_LOOK:   return "LOOK";
        case AUTO_WINK:   return "WINK";
        case AUTO_SMILE:  return "SMILE";
        case AUTO_PLAY:   return "PLAY_INVITE";
        default:          return "NONE";
    }
}


uint32_t EyesModel::chooseAutonomousDelayMs() {
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

  if (roll < shortWeight)
    return (uint32_t)random((long)AUTO_SHORT_MIN_MS, (long)AUTO_SHORT_MAX_MS + 1L);

  roll -= shortWeight;
  if (roll < mediumWeight)
    return (uint32_t)random((long)AUTO_MEDIUM_MIN_MS, (long)AUTO_MEDIUM_MAX_MS + 1L);

  return (uint32_t)random((long)AUTO_LONG_MIN_MS, (long)AUTO_LONG_MAX_MS + 1L);
}

void EyesModel::scheduleNextAutonomous(uint32_t now) {
  autonomousState.nextAt = now + chooseAutonomousDelayMs();
}

void EyesModel::rememberAutonomous(uint8_t state) {
  recentAutonomous[2] = recentAutonomous[1];
  recentAutonomous[1] = recentAutonomous[0];
  recentAutonomous[0] = (AutonomousStateType)state;
}

int EyesModel::applyAutonomousHistoryPenalty(uint8_t state, int weight) {
  if (recentAutonomous[0] == state) {
    weight = (weight * 25) / 100;
  } else if (recentAutonomous[1] == state || recentAutonomous[2] == state) {
    weight = (weight * 60) / 100;
  }

  if (weight < 1) weight = 1;
  return weight;
}

AutonomousStateType EyesModel::chooseAutonomousState(uint32_t now) {
  int weights[4] = {
    30, // LOOK
    20, // WINK
    20, // SMILE
    18  // PLAY
  };

  // Personality gets richer by stage without removing any generic behavior family.
  if (demo.stage == 2) {
    weights[1] += 2;
    weights[2] += 3;
    weights[3] += 3;
  } else if (demo.stage == 3) {
    weights[1] += 3;
    weights[2] += 5;
    weights[3] += 5;
  }

  // A longer quiet period makes a friendly play invitation more likely, but
  // never schedules it at a deterministic timeout.
  uint32_t quietMs = now - demo.clock.lastInteractionMs;
  if (quietMs >= 30UL * 1000UL) weights[3] += 4;
  if (quietMs >= 60UL * 1000UL) weights[3] += 5;
  if (quietMs >= 120UL * 1000UL) weights[3] += 6;

  // Small independent per-cycle jitter changes effective priority.
  for (int i = 0; i < 4; i++) {
    weights[i] += (int)random(0, 11) - 5;
    if (weights[i] < 1) weights[i] = 1;
  }

  const AutonomousStateType states[4] = {
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

uint32_t EyesModel::chooseAutonomousDurationMs(uint8_t state) {
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

void EyesModel::cancelAutonomous() {
  autonomousState.type = AUTO_NONE;
  autonomousState.start = 0;
  autonomousState.duration = 0;
}

void EyesModel::interruptAutonomousForInteraction(uint32_t now) {
  if (autonomousState.type != AUTO_NONE) {
    cancelAutonomous();
    blinkState = BLINK_OPEN;
    blinkAmount = 0.0f;
    scheduleNextBlink(now);
  }
}

void EyesModel::startAutonomous(AutonomousStateType type, uint32_t now) {
  if (reaction != R_NONE || demo.sleepRequestPending) return;
  if (!userReactionCanStart(now)) return;

  // autonomousState = state;
  autonomousState.type = type;
  autonomousState.start = now;
  autonomousState.duration = chooseAutonomousDurationMs(autonomousState.type);
  autonomousState.variant = (uint8_t)random(4);
  autonomousState.winkLeft = random(2) == 0;

  // Personality events own the eyelid/gaze presentation while active.
  blinkState = BLINK_OPEN;
  blinkAmount = 0.0f;

  rememberAutonomous(autonomousState.type);

  Serial.print("AUTONOMOUS -> ");
  Serial.print(autonomousState.getName(autonomousState.type));
  Serial.print(" variant=");
  Serial.print(autonomousState.variant);
  Serial.print(" duration=");
  Serial.print(autonomousState.duration);
  Serial.println(" ms");
}

void EyesModel::updateAutonomous(uint32_t now) {
  if (reaction != R_NONE || demo.sleepRequestPending) {
    if (autonomousState.type != AUTO_NONE) cancelAutonomous();
    return;
  }

  if (autonomousState.type != AUTO_NONE) {
    if ((now - autonomousState.start) >= autonomousState.duration) {
      cancelAutonomous();
      chooseIdleLook(now);
      scheduleNextBlink(now);
      scheduleNextAutonomous(now);
    }
    return;
  }

  if (!userReactionCanStart(now)) return;

  if (autonomousState.nextAt == 0) {
    scheduleNextAutonomous(now);
    return;
  }

  if ((int32_t)(now - autonomousState.nextAt) >= 0) {
    startAutonomous(chooseAutonomousState(now), now);
  }
}

float EyesModel::autonomousLidAmount(bool leftSide, uint32_t now) const {
  if (reaction != R_NONE || autonomousState.type == AUTO_NONE || autonomousState.duration == 0) {
    return 0.0f;
  }

  float p = clamp01((float)(now - autonomousState.start) / (float)autonomousState.duration);

  if (autonomousState.type == AUTO_WINK) {
    bool targetEye = autonomousState.winkLeft ? leftSide : !leftSide;
    if (!targetEye) return 0.0f;

    if (p < 0.28f) return smoothStep(p / 0.28f);
    if (p < 0.62f) return 1.0f;
    return 1.0f - smoothStep((p - 0.62f) / 0.38f);
  }

  // One Play variant contains a brief half-wink; the other variants use only
  // gaze/bounce changes. This adds variation without turning Play into Wink.
  if (autonomousState.type == AUTO_PLAY && autonomousState.variant == 2) {
    bool targetEye = autonomousState.winkLeft ? leftSide : !leftSide;
    if (!targetEye || p < 0.42f || p > 0.76f) return 0.0f;
    float local = (p - 0.42f) / 0.34f;
    return sinf(local * PI) * 0.60f;
  }

  return 0.0f;
}

float EyesModel::petRequestLidAmount(uint32_t now) const {
  if (!petRequest.promptActive || reaction != R_NONE) return 0.0f;

  uint32_t elapsed = now - petRequest.promptStart;
  if (elapsed >= PET_REQUEST_DURATION_MS) return 0.0f;

  float p = (float)elapsed / (float)PET_REQUEST_DURATION_MS;
  // Two very shallow "please?" eyelid softens; never approaches a blink.
  float pulse = 0.5f + 0.5f * sinf(p * TWO_PI * 2.0f - PI * 0.5f);
  return pulse * 0.09f;
}

float EyesModel::sleepRequestLidAmount(uint32_t now) const {
  if (!demo.sleepRequestPending || reaction != R_NONE) return 0.0f;

  // 35-55% drowsy upper-lid closure: distinct from both a normal blink and
  // the fully closed persistent R_SLEEP state.
  float slowMotion = sinf((float)now * 0.00135f) * 0.05f;
  return 0.45f + slowMotion;
}

void EyesModel::applyAutonomousEyeTargets(uint32_t now) {
  if (autonomousState.type == AUTO_NONE || autonomousState.duration == 0) return;

  uint32_t elapsed = now - autonomousState.start;
  float p = clamp01((float)elapsed / (float)autonomousState.duration);
  float stageScale = (demo.stage == 1) ? 0.85f : (demo.stage == 2 ? 1.0f : 1.15f);

  if (autonomousState.type == AUTO_LOOK) {
    specialGlowTarget = (demo.stage == 1) ? 0.02f : (demo.stage == 2 ? 0.09f : 0.17f);

    if (autonomousState.variant == 0) {
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
    } else if (autonomousState.variant == 1) {
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

  if (autonomousState.type == AUTO_WINK) {
    happyTarget = 0.22f * stageScale;
    specialGlowTarget = 0.08f * stageScale;
    float side = autonomousState.winkLeft ? -1.0f : 1.0f;
    leftEye.targetX = side * 4.0f;
    rightEye.targetX = side * 4.0f;
    leftEye.targetY = -2.0f;
    rightEye.targetY = -2.0f;
    return;
  }

  if (autonomousState.type == AUTO_SMILE) {
    float enter = smoothStep(clamp01((float)elapsed / 450.0f));
    float exit = 1.0f;
    if (autonomousState.duration > 550UL && elapsed > autonomousState.duration - 550UL) {
      exit = 1.0f - smoothStep(
        clamp01((float)(elapsed - (autonomousState.duration - 550UL)) / 550.0f)
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

  if (autonomousState.type == AUTO_PLAY) {
    float enter = smoothStep(clamp01((float)elapsed / 350.0f));
    float exit = 1.0f;
    if (autonomousState.duration > 500UL && elapsed > autonomousState.duration - 500UL) {
      exit = 1.0f - smoothStep(
        clamp01((float)(elapsed - (autonomousState.duration - 500UL)) / 500.0f)
      );
    }
    float strength = enter * exit;

    happyTarget = 0.30f * stageScale * strength;
    surpriseTarget = 0.38f * stageScale * strength;
    specialGlowTarget = 0.16f * stageScale * strength;

    if (autonomousState.variant == 0) {
      float sweep = sinf(p * TWO_PI * 1.15f) * 12.0f;
      leftEye.targetX = sweep;
      rightEye.targetX = sweep;
      leftEye.targetY = -3.0f - fabsf(sinf(p * TWO_PI * 1.15f)) * 3.0f * stageScale;
      rightEye.targetY = leftEye.targetY;
    } else if (autonomousState.variant == 1) {
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

void EyesModel::applyPetRequestEyeCue(uint32_t now) {
  if (!petRequest.promptActive || reaction != R_NONE) return;

  uint32_t elapsed = now - petRequest.promptStart;
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

void EyesModel::applySleepRequestEyeCue(uint32_t now) {
  if (!demo.sleepRequestPending || reaction != R_NONE) return;

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

void EyesModel::updateBlink(uint32_t now) {
  switch (blinkState) {
    case BLINK_OPEN:
      blinkAmount = 0.0f;

      if (reaction == R_NONE &&
          autonomousState.type == AUTO_NONE &&
          !demo.sleepRequestPending &&
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

void EyesModel::updateEyeTargets(uint32_t now) {
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
    if (demo.sleepRequestPending) {
      applySleepRequestEyeCue(now);
      return;
    }

    // Stage identity remains present beneath the autonomous personality layer.
    if (demo.stage == 3) {
      float tiny = sinf(now * 0.0017f) * 0.7f;
      leftEye.targetY += tiny;
      rightEye.targetY += tiny;
      specialGlowTarget = 0.16f;
    } else if (demo.stage == 2) {
      specialGlowTarget = 0.08f;
    }

    if (autonomousState.type != AUTO_NONE) {
      applyAutonomousEyeTargets(now);
      leftEye.targetX  *= 1.3f;
      rightEye.targetX *= 1.3f;
      if (leftEye.targetY  < 0) leftEye.targetY  *= 1.4f;
      if (rightEye.targetY < 0) rightEye.targetY *= 1.4f;
    }

    if (petRequest.promptActive) {
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
    if (sleepReaction.sleepTagPresent) {
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
      uint32_t wakeElapsed = (sleepReaction.sleepWakeStart == 0) ? 0 : (now - sleepReaction.sleepWakeStart);
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

void EyesModel::updateEyeDynamics(float dt) {
  const float gazeSpeed = 3.0f;

  leftEye.x = smoothFollow(leftEye.x, leftEye.targetX, dt, gazeSpeed);
  leftEye.y = smoothFollow(leftEye.y, leftEye.targetY, dt, gazeSpeed);

  rightEye.x = smoothFollow(rightEye.x, rightEye.targetX, dt, gazeSpeed);
  rightEye.y = smoothFollow(rightEye.y, rightEye.targetY, dt, gazeSpeed);

  happyBlend = smoothFollow(happyBlend, happyTarget, dt, 7.0f);
  surpriseBlend = smoothFollow(surpriseBlend, surpriseTarget, dt, 7.0f);
  sleepClose = smoothFollow(sleepClose, sleepCloseTarget, dt, 8.0f);
  specialGlow = smoothFollow(specialGlow, specialGlowTarget, dt, 6.0f);
}

float EyesModel::getRingPulseAmount(uint32_t now) const {
  uint32_t elapsed = now - ringPulseStart;
  if (ringPulseStart == 0 || elapsed >= RING_PULSE_MS) return 0.0f;

  float p = (float)elapsed / (float)RING_PULSE_MS;
  return sinf(p * PI);
}