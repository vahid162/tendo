#include "DemoClock.h"
#include <Arduino.h>


uint32_t DemoClock::getActiveElapsedMs(uint32_t now) {
  // if (completed) return totalDemoMs;

  uint32_t elapsed = savedActiveMs;

  if (running)
    elapsed += (now - activeRunStartMs);

  if (elapsed > totalDemoMs)
    elapsed = totalDemoMs;

  return elapsed;
}

uint32_t DemoClock::getStageActiveElapsedMs(uint32_t now, uint8_t stage) {
  uint32_t elapsed = getActiveElapsedMs(now);
  //TODO: this STAGE_MS should not be accessible from this function
  uint32_t stageStart = (uint32_t)(stage - 1) * STAGE_MS;

  if (elapsed <= stageStart) return 0;

  uint32_t stageElapsed = elapsed - stageStart;
  return (stageElapsed > STAGE_MS) ? STAGE_MS : stageElapsed;
}



void DemoClock::reset(uint32_t now) {
  started = false;
  running = false;
  savedActiveMs = 0;
  activeRunStartMs = 0;
  lastInteractionMs = now;
  lastNvsCheckpointElapsed = 0;
}



void DemoClock::restore(uint32_t savedActiveMs, bool started, uint32_t now) {
  this->started = started;
  lastInteractionMs = now;
  this->savedActiveMs = (savedActiveMs > TOTAL_DEMO_MS) ? TOTAL_DEMO_MS : savedActiveMs;
  running = false;
  activeRunStartMs = 0;
}


void DemoClock::finish() {
  savedActiveMs = TOTAL_DEMO_MS;
  running = false;
  activeRunStartMs = 0;
}
