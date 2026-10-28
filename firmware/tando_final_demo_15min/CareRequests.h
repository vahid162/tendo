#pragma once

#include "TandoTypes.h"

// The request families share one timeline; Demo persists and restores these fields.
extern uint8_t hungerRequestStage;
extern uint8_t hungerRequestsShown;
extern uint32_t nextHungerRequestAt;
extern uint8_t hungerRequestSlot;
extern bool hungerPromptActive;
extern bool hungerPromptTracked;
extern bool hungerPromptManualPreview;
extern uint32_t hungerPromptStart;
extern bool hungerRetryPending;
extern uint8_t petRequestStage;
extern uint8_t petRequestsShown;
extern uint32_t nextPetRequestAt;
extern uint8_t petRequestSlot;
extern bool petRequestPromptActive;
extern bool petRequestPromptTracked;
extern bool petRequestPromptManualPreview;
extern uint32_t petRequestPromptStart;
extern bool petRequestRetryPending;
extern uint32_t careStageWallStartMs;
extern bool careStageWallClockReady;
extern uint32_t careRequestNextAllowedWallMs;

uint32_t getCurrentCareStageWallMs(uint32_t now);
bool hungerNeedSatisfiedThisStage();
bool hungerOverlayBlockedByPriority();
void clearHungerPromptRuntime();
void startHungerPrompt(uint32_t now, bool tracked);
void interruptTrackedHungerPromptForRetry(uint32_t now);
void resetHungerRequestForStage(uint32_t now);
void syncHungerRequestStage(uint32_t now);
void satisfyHungerNeedForStage(uint32_t now);
void updateHungerRequestScheduler(uint32_t now);
bool petRequestNeedSatisfiedThisStage();
bool petRequestBlockedByPriority();
void clearPetRequestPromptRuntime();
void startPetRequestPrompt(uint32_t now, bool tracked);
void interruptTrackedPetRequestForRetry(uint32_t now);
void resetPetRequestForStage(uint32_t now);
void syncPetRequestStage(uint32_t now);
void satisfyPetRequestNeedForStage(uint32_t now);
void updatePetRequestScheduler(uint32_t now);
