#ifndef TANDO_CONTROLLER_H
#define TANDO_CONTROLLER_H

#include <Arduino.h>
#include "../model/Reaction.h"
#include "../model/Request.h"

class DemoController {
public:
  void boot(); 
  void update(); 
  void resetDemo(uint32_t now);

private:
  uint32_t lastFrame = 0;
  uint32_t lastTouchPoll = 0;
  uint32_t lastRfidPoll = 0;
  uint32_t previousFrameTime = 0;

  void initCareClock(uint32_t now, bool ready);
  void printBanner();
  // bool faceBlockedForRequests();
};

extern DemoController demoController;
extern FoodReaction foodReaction;
extern PetReaction petReaction;
extern ConfusedReaction confusedReaction;
extern CompletionReaction completionReaction;
extern UnlockReaction unlockReaction;
extern SleepReaction sleepReaction;
extern LEDReaction ledReaction;
extern HungerRequest hungerRequest;
extern PetRequest petRequest;


#endif 