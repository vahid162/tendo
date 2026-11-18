#pragma once

#include "../TandoTypes.h"
#include "../TandoConfig.h"

class Request {
    public:
    uint8_t requestStage;
    uint8_t requestsShown;
    uint32_t nextRequestAt;
    uint8_t requestSlot;
    uint8_t nextRequestSlot;
    bool promptActive;
    bool promptTracked;
    bool promptManualPreview;
    uint32_t promptStart;
    bool retryPending;
    int requestsPerStage;
    String requestName;
    uint32_t requestDuration;
    uint8_t requestBit;
    
    Request() 
    : requestStage(0),
    requestsShown(0),
    nextRequestAt(0),
    requestSlot(0),
    nextRequestSlot(0),
    promptActive(false),
    promptTracked(false),
    promptManualPreview(false),
    promptStart(0),
    retryPending(false) {}

    bool needSatisfiedThisStage();
    bool blockedByPriority();
    void clearPromptRuntime();
    void startPrompt(uint32_t now, bool tracked);
    void interruptTrackedRequestPromptForRetry(uint32_t now);
    void resetRequestForStage(uint32_t now);
    void syncRequestStage(uint32_t now);
    void satisfyNeedForStage(uint32_t now);
    void updateRequestScheduler(uint32_t now);
    void scheduleNextRequest(uint32_t now, bool retrySoon);
    void completeTrackedRequest(uint32_t now);
    void reset();
    void restore(uint8_t stage, uint8_t shown);
};

class PetRequest : public Request {
    public:
    PetRequest(String requestName) {
        this->requestName = requestName;
        this->requestDuration = PET_REQUEST_DURATION_MS;
        this->requestsPerStage = PET_REQUESTS_PER_STAGE;
        this->requestBit = CARE_PET_BIT;

    }
};

class HungerRequest : public Request {
    public:
    HungerRequest(String requestName) {
        this->requestName = requestName;
        this->requestDuration = HUNGER_REQUEST_DURATION_MS;
        this->requestsPerStage = HUNGER_REQUESTS_PER_STAGE;
        this->requestBit = CARE_FOOD_BIT;
    }
};

//TOOD: overlaying thing should be though about
// class CareRequestArbiter {
    // private:
    //     const int max = REQ_COUNT;
//     Request* requestList[REQ_COUNT];
//     int n = 0; 

// public:
//     void add(Request& request){
    
//     }

// };



extern uint32_t careStageWallStartMs;
extern bool careStageWallClockReady;
extern uint32_t careRequestNextAllowedWallMs;

uint32_t getCurrentCareStageWallMs(uint32_t now);