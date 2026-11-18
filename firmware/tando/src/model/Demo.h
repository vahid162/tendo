#ifndef TANDO_DEMO_H
#define TANDO_DEMO_H

#include <Arduino.h>
#include "DemoClock.h"
#include "DemoStorage.h"

class Demo {
public:
    bool completed;
    uint8_t stage;
    uint8_t stageCareMask;
    uint8_t visualCredits;
    DemoClock clock;
    DemoStorage storage;
    bool sleepRequestPending = false;
    uint8_t sleepCreditWindowAnnouncedStage = 0;
    
    Demo()
    : completed(false),
    stage(1),
    stageCareMask(0),
    visualCredits(0) {}
    
    void begin(uint32_t now);
    void save(uint32_t now);

    bool sleepCreditWindowOpen(uint32_t now);
    bool sleepRequestStartReached(uint32_t now);
    bool sleepCreditedThisStage();
    void restore(uint32_t now);
    void reset(uint32_t now);
    void notifyUserInteraction(uint32_t now);
    void printProgressState();
    bool registerCareCredit(uint8_t careBit, const char *label, uint32_t now);
    void autoFillPetAndFoodCreditsForClosingStage();
    bool grantSleepCreditForAcceptedTag(uint32_t now);
    void updateSleepNeedState(uint32_t now, bool sleeping);
    void updateDemoClock(uint32_t now);
    void updateStageByTime(uint32_t now);
    void resetTransient();
    
    //getter functions
    bool isCompleted() const { return completed; }
};

extern Demo demo;

#endif