#pragma once

#include "TandoTypes.h"

// Internal stage/time interface; mutable storage is defined once in Demo.cpp.
extern bool demoStarted;
extern bool demoClockRunning;
extern bool completionFlag;
extern uint32_t lastInteractionMs;
extern uint8_t currentStage;
extern uint8_t stageCareMask;
extern uint8_t visualCredits;
extern bool sleepRequestPending;

uint32_t getActiveElapsedMs(uint32_t now);
uint32_t getStageActiveElapsedMs(uint32_t now);
bool sleepCreditWindowOpen(uint32_t now);
bool sleepCreditedThisStage();
void savePersistentState(uint32_t now);
void resetDemoState(uint32_t now);
void notifyUserInteraction(uint32_t now);
void printProgressState();
bool registerCareCredit(uint8_t careBit, const char *label, uint32_t now);
bool grantSleepCreditForAcceptedTag(uint32_t now);
void updateSleepNeedState(uint32_t now);
void updateDemoClock(uint32_t now);
void setupDemoStorage();
void restoreDemoRuntime(uint32_t now);
