#include "Touch.h"
#include "TandoConfig.h"
#include "Reactions.h"
#include <Wire.h>
#include <Adafruit_MPR121.h>

// Owns MPR121 sensing, manual recalibration, and the latched two-pad PET detector.

// Local declarations replace Arduino-generated prototypes.
static uint8_t countPetBits(uint16_t mask);
static void resetPetDetector();
static void printPetMask(uint16_t mask, uint32_t stableMs);

static Adafruit_MPR121 mpr;
static bool petGestureLocked = false;
static bool petTwoPadCandidate = false;
static uint32_t petTwoPadSince = 0;
static uint32_t petBelowTwoSince = 0;
static uint32_t lastPetTriggerAt = 0;
static bool hasPetTrigger = false;

uint8_t countPetBits(uint16_t mask) {
  uint8_t count = 0;
  if (mask & PET_E0_MASK) count++;
  if (mask & PET_E6_MASK) count++;
  if (mask & PET_E11_MASK) count++;
  return count;
}

void resetPetDetector() {
  petGestureLocked = false;
  petTwoPadCandidate = false;
  petTwoPadSince = 0;
  petBelowTwoSince = 0;
  hasPetTrigger = false;
  lastPetTriggerAt = 0;
}

bool recalibrateMpr121(uint32_t now) {
  // Manual diagnostic recovery only. Do not auto-recalibrate during normal
  // play because a real hand could be present and become part of the baseline.
  Serial.println("MPR121 RECALIBRATION - KEEP HAND AWAY");
  delay(600);

  if (!mpr.begin(
        0x5A,
        &Wire,
        MPR_TOUCH_THRESHOLD,
        MPR_RELEASE_THRESHOLD,
        true)) {
    Serial.println("MPR121 RECALIBRATION FAILED");
    return false;
  }

  resetPetDetector();
  lastPetTriggerAt = now;

  delay(300);
  Serial.println("MPR121 RECALIBRATION COMPLETE");
  printMprDiagnostics();
  return true;
}

void printPetMask(uint16_t mask, uint32_t stableMs) {
  Serial.print("PET CAPACITIVE: ");

  bool first = true;
  if (mask & PET_E0_MASK) {
    Serial.print("E0");
    first = false;
  }
  if (mask & PET_E6_MASK) {
    if (!first) Serial.print("+");
    Serial.print("E6");
    first = false;
  }
  if (mask & PET_E11_MASK) {
    if (!first) Serial.print("+");
    Serial.print("E11");
  }

  Serial.print(" | stable=");
  Serial.print(stableMs);
  Serial.println(" ms -> PET");
}

void printMprDiagnostics() {
  uint16_t hwTouched = mpr.touched() & PET_ALL_MASK;
  Serial.println("---- MPR121 PET E0/E6/E11 ----");
  for (uint8_t i = 0; i < PET_ELECTRODE_COUNT; i++) {
    uint8_t electrode = PET_ELECTRODES[i];
    uint16_t mask = PET_ELECTRODE_MASKS[i];
    uint16_t baseline = mpr.baselineData(electrode);
    uint16_t filtered = mpr.filteredData(electrode);
    int16_t delta = (int16_t)baseline - (int16_t)filtered;

    Serial.print("E");
    Serial.print(electrode);
    Serial.print(" baseline=");
    Serial.print(baseline);
    Serial.print(" filtered=");
    Serial.print(filtered);
    Serial.print(" delta=");
    Serial.print(delta);
    Serial.print(" touched=");
    Serial.println((hwTouched & mask) ? "YES" : "NO");
  }

  Serial.print("thresholds touch/release = ");
  Serial.print(MPR_TOUCH_THRESHOLD);
  Serial.print("/");
  Serial.println(MPR_RELEASE_THRESHOLD);
  Serial.print("MPR121 ECR=0x");
  Serial.println(mpr.readRegister8(MPR121_ECR), HEX);
  Serial.print("touched baseline filter NHDT/NCLT/FDLT (read-only) = ");
  Serial.print(mpr.readRegister8(MPR121_NHDT), HEX);
  Serial.print("/");
  Serial.print(mpr.readRegister8(MPR121_NCLT), HEX);
  Serial.print("/");
  Serial.println(mpr.readRegister8(MPR121_FDLT), HEX);

  Serial.print("pet state: touchedCount=");
  Serial.print(countPetBits(hwTouched));
  Serial.print(" locked=");
  Serial.print(petGestureLocked ? "YES" : "NO");
  Serial.print(" candidate=");
  Serial.print(petTwoPadCandidate ? "YES" : "NO");
  Serial.print(" belowTwoSince=");
  Serial.println(petBelowTwoSince);
  Serial.println("--------------------------");
}

void updateTouch(uint32_t now) {
  uint16_t rawTouched = mpr.touched() & PET_ALL_MASK;
  uint8_t touchedCount = countPetBits(rawTouched);

  // PET is completely disabled during persistent sleep.
  if (reaction == R_SLEEP || pendingSleepVisual) {
    petGestureLocked = false;
    petTwoPadCandidate = false;
    petTwoPadSince = 0;
    petBelowTwoSince = 0;
    return;
  }

  // After firing once, do not repeatedly trigger on the exact same continuous
  // >=2-pad hold. Re-arm as soon as fewer than two pads stay active for a
  // short stable period. A single stuck/residual YES electrode is acceptable.
  if (petGestureLocked) {
    if (touchedCount < 2) {
      if (petBelowTwoSince == 0) petBelowTwoSince = now;

      if ((now - petBelowTwoSince) >= PET_REARM_CLEAR_MS &&
          (!hasPetTrigger || (now - lastPetTriggerAt) >= PET_EVENT_REFRACTORY_MS)) {
        petGestureLocked = false;
        petBelowTwoSince = 0;
        Serial.println("PET REARMED - LIVE TOUCH COUNT BELOW 2");
      }
    } else {
      petBelowTwoSince = 0;
    }
    return;
  }

  // New rule: live 2-of-3 or 3-of-3 is itself the PET qualification.
  if (touchedCount >= 2) {
    if (!petTwoPadCandidate) {
      petTwoPadCandidate = true;
      petTwoPadSince = now;
      return;
    }

    uint32_t stableMs = now - petTwoPadSince;
    if (stableMs >= PET_TWO_PAD_CONFIRM_MS &&
        (!hasPetTrigger || (now - lastPetTriggerAt) >= PET_EVENT_REFRACTORY_MS)) {
      printPetMask(rawTouched, stableMs);
      handlePetEvent(now);

      petGestureLocked = true;
      petTwoPadCandidate = false;
      petTwoPadSince = 0;
      petBelowTwoSince = 0;
      lastPetTriggerAt = now;
      hasPetTrigger = true;
    }
    return;
  }

  // Fewer than two live touched electrodes means there is no PET candidate.
  petTwoPadCandidate = false;
  petTwoPadSince = 0;
}

void setupTouch() {
  // ----------------------------------------------------------
  // MPR121
  // ----------------------------------------------------------
  Wire.begin(MPR_SDA, MPR_SCL);

  // rc.3 sensing path: final thresholds + autoconfiguration are supplied
  // directly to begin(). No experimental touched-baseline filter tuning.
  if (!mpr.begin(
        0x5A,
        &Wire,
        MPR_TOUCH_THRESHOLD,
        MPR_RELEASE_THRESHOLD,
        true)) {
    Serial.println("FATAL: MPR121 NOT FOUND");
    while (true) delay(100);
  }

  Serial.print("MPR121 READY - touch/release thresholds: ");
  Serial.print(MPR_TOUCH_THRESHOLD);
  Serial.print("/");
  Serial.print(MPR_RELEASE_THRESHOLD);
  Serial.print(" | ECR=0x");
  Serial.println(mpr.readRegister8(MPR121_ECR), HEX);
}
