#pragma once

#include "TandoTypes.h"

// Shared reaction state; definitions and priority/queue handling live in Reactions.cpp.
extern uint8_t pendingUnlockMask;
extern bool pendingCompletion;
extern bool sleepTagPresent;
extern uint32_t sleepWakeStart;
extern Reaction reaction;
extern uint32_t reactionStart;
extern bool pendingPetVisual;
extern uint8_t pendingFoodVisual;
extern bool pendingConfusedVisual;
extern bool pendingSleepVisual;

void setupReactionLed();
void updateReactionLed(uint32_t now);
bool userReactionCanStart(uint32_t now);
bool reactionIsBusy();
void clearBlinkForExclusiveReaction();
void beginSleepWake(uint32_t now);
void handlePetEvent(uint32_t now);
void handleFoodEvent(uint8_t foodNumber, uint32_t now);
void handleSleepEvent(uint32_t now);
void handleUnknownRfidEvent(uint32_t now);
void servicePendingSystemReaction(uint32_t now);
void servicePendingUserReaction(uint32_t now);
void updateReaction(uint32_t now);
