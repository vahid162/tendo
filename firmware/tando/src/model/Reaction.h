#pragma once

#include "../TandoConfig.h"
#include  "../TandoTypes.h"
#include "Arduino.h"

// Shared reaction state; definitions and priority/queue handling live in Reactions.cpp.
extern uint8_t pendingUnlockMask;
extern ReactionType reaction;
extern uint32_t reactionStart;


class Reaction {
    public:
    ReactionType type;
    uint32_t startTime;
    bool pending;

    void start(uint32_t now) {}; //overriding in children
    // void finish(uint32_t now);
    // void updateReaction(uint32_t now);
    void handleEvent() {}; //overloading in children

};

class FoodReaction : public Reaction {
    public:
    uint8_t foodPending;
    
    FoodReaction() {
        this->type = ReactionType::R_FOOD;
    }

    void start(uint32_t now);
    void handleEvent(uint8_t foodNumber, uint32_t now);
};

class PetReaction : public Reaction {
public:
PetReaction() {
        this->type = ReactionType::R_PET;
    }
    
    void start(uint32_t now);
    void handleEvent(uint32_t now);
};


void setupReactionLed();
void updateReactionLed(uint32_t now);
bool userReactionCanStart(uint32_t now);
bool reactionIsBusy();
void clearBlinkForExclusiveReaction();
void handleUnknownRfidEvent(uint32_t now);
void servicePendingSystemReaction(uint32_t now);
void servicePendingUserReaction(uint32_t now);
void updateReaction(uint32_t now);

class LEDReaction : public Reaction {
    public:
    bool ledPulseActive;
    uint32_t ledPulseStart;
    uint8_t ledPulsePeak;
    uint8_t ledPulseRepeats;
    
    LEDReaction() :
    ledPulseActive(false),
    ledPulseStart(0),
        ledPulsePeak(LED_REACTION_PEAK),
        ledPulseRepeats(1) {
        this->type = ReactionType::R_LED;
    }

    // void start(uint32_t now);
    // void setup();
    // void updateReaction(uint32_t now);
};

class SleepReaction : public Reaction {
public:
    bool sleepTagPresent;
    uint32_t sleepWakeStart;

    SleepReaction() 
        : sleepTagPresent(false),
          sleepWakeStart(0) {
        this->type = ReactionType::R_SLEEP;
    }

    void start(uint32_t now);
    void handleEvent(uint32_t now);
    void beginSleepWake(uint32_t now);
};


class ConfusedReaction : public Reaction {
    public:
    ConfusedReaction() {
        this->type = ReactionType::R_CONFUSED;
    }

    void start(uint32_t now);
};


class CompletionReaction : public Reaction {
public:
CompletionReaction() {
        this->type = ReactionType::R_COMPLETE;
    }
    void start(uint32_t now);
};


class UnlockReaction : public Reaction {
public:
    UnlockReaction() {
        
    }

    void start(uint8_t stageNumber, uint32_t now);
};


// class ReactionManager {
// public:
//     bool isbusy() {};
// };

