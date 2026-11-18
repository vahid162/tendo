#include "DemoStorage.h"
#include "../TandoConfig.h"

void DemoStorage::save(const DemoSnapshot& snapshot) {
  // uint32_t elapsed = getActiveElapsedMs(now);

  prefs.putUInt("ver", NVS_STATE_VERSION);
  prefs.putUInt("elapsed", snapshot.elapsedMs);
  prefs.putUChar("stage", snapshot.stage);
  prefs.putUChar("credits", snapshot.credits);
  prefs.putUChar("mask", snapshot.careMask);
  prefs.putBool("started", snapshot.started);
  prefs.putBool("done", snapshot.completed);
  prefs.putUChar("hStage", snapshot.hungerStage);
  prefs.putUChar("hCount", snapshot.hungerShown);
  prefs.putUChar("pStage", snapshot.petStage);
  prefs.putUChar("pCount", snapshot.petShown);
  // lastNvsCheckpointElapsed = snapshot.elapsedMs;
}

void DemoStorage::clear() {
  prefs.clear();
  prefs.putUInt("ver", NVS_STATE_VERSION);
}


bool DemoStorage::load(DemoSnapshot& snapshot) {
  snapshot = DemoSnapshot();

  uint32_t version = prefs.getUInt("ver", 0);
  if (version != NVS_STATE_VERSION) {
    clear();
    return false;
  }

  snapshot.elapsedMs = prefs.getUInt("elapsed", 0);
  snapshot.stage = prefs.getUChar("stage", 1);
  snapshot.started = prefs.getBool("started", false);
  snapshot.completed = prefs.getBool("done", false);
  snapshot.credits = prefs.getUChar("credits", 0);
  snapshot.careMask = prefs.getUChar("mask", 0);
  snapshot.hungerStage = prefs.getUChar("hStage", 0);
  snapshot.hungerShown = prefs.getUChar("hCount", 0);
  snapshot.petStage = prefs.getUChar("pStage", 0);
  snapshot.petShown = prefs.getUChar("pCount", 0);

  return true;
}





void DemoStorage::begin() {
  // ----------------------------------------------------------
  // NVS
  // ----------------------------------------------------------
  if (!prefs.begin("tandoDemo", false)) {
    Serial.println("FATAL: NVS / Preferences open failed");
    while (true) delay(1000);
  }
}


