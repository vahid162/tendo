#ifndef TANDO_TYPES_H
#define TANDO_TYPES_H

#include <Arduino.h>

struct EyeMotion {
  float x;
  float y;
  float targetX;
  float targetY;
};

enum BlinkState : uint8_t {
  BLINK_OPEN,
  BLINK_CLOSING,
  BLINK_CLOSED,
  BLINK_OPENING
};

enum ReactionType : uint8_t {
  R_NONE,
  R_PET,
  R_FOOD,
  R_SLEEP,
  R_CONFUSED,
  R_UNLOCK2,
  R_UNLOCK3,
  R_COMPLETE,
  R_LED,
  R_COUNT
};

enum AutonomousStateType : uint8_t {
  AUTO_NONE,
  AUTO_LOOK,
  AUTO_WINK,
  AUTO_SMILE,
  AUTO_PLAY
};

enum class Stage : uint8_t {
  NONE = 0,
  ONE,
  TWO,
  THREE
};


#endif 