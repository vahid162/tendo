#ifndef TANDO_DEMO_CLOCK_H
#define TANDO_DEMO_CLOCK_H

#include <Arduino.h>
#include "../TandoConfig.h"

class DemoClock {
public:
    bool started;
    bool running;
    uint32_t totalDemoMs;
    uint32_t savedActiveMs;
    uint32_t activeRunStartMs;
    uint32_t lastInteractionMs;
    uint32_t lastNvsCheckpointElapsed;
    
    DemoClock()
    : started(false),
    running(false),
    totalDemoMs(TOTAL_DEMO_MS),
    savedActiveMs(0),
    activeRunStartMs(0),
    lastInteractionMs(0),
    lastNvsCheckpointElapsed(0) {}
    
    uint32_t getActiveElapsedMs(uint32_t now);
    uint32_t getStageActiveElapsedMs(uint32_t now, uint8_t stage);
    void reset(uint32_t now);
    void restore(uint32_t savedActiveMs, bool started, uint32_t now);
    void finish();
};

#endif