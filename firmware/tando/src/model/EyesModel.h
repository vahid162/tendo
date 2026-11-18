#ifndef TANDO_EYES_MODEL_H
#define TANDO_EYES_MODEL_H

#include <Arduino.h>
#include "../TandoTypes.h"

class AutonomousState {
public:
    AutonomousStateType type;
    uint32_t start;
    uint32_t duration;
    uint32_t nextAt;
    uint8_t variant;
    bool winkLeft;

    AutonomousState() 
        : type(AUTO_NONE),
          start(0),
          duration(0),
          nextAt(0),
          variant(0),
          winkLeft(true) {}

    const char* getName(AutonomousStateType type);

};

class EyesModel {
public:
  EyeMotion leftEye  = {0, 0, 0, 0};
  EyeMotion rightEye = {0, 0, 0, 0};
  uint32_t nextIdleLook = 0;
  float happyBlend = 0.0f;
  float surpriseBlend = 0.0f;
  float sleepClose = 0.0f;
  float specialGlow = 0.0f;
  BlinkState blinkState = BLINK_OPEN;
  float blinkAmount = 0.0f;
  uint32_t blinkStart = 0;
  uint32_t nextBlink = 0;
  AutonomousState autonomousState;

  static float clamp01(float v);
  static float smoothFollow(float current, float target, float dt, float speed);
  static float mapF(float value, float inMin, float inMax, float outMin, float outMax);

  void triggerRingPulse(uint32_t now);
  float getRingPulseAmount(uint32_t now) const;

  void scheduleNextBlink(uint32_t now);
  void startBlink(uint32_t now);
  void chooseIdleLook(uint32_t now);
  void updateBlink(uint32_t now);

  void scheduleNextAutonomous(uint32_t now);
  void cancelAutonomous();
  void interruptAutonomousForInteraction(uint32_t now);
  void startAutonomous(AutonomousStateType type, uint32_t now);
  void updateAutonomous(uint32_t now);

  float autonomousLidAmount(bool leftSide, uint32_t now) const;
  float petRequestLidAmount(uint32_t now) const;
  float sleepRequestLidAmount(uint32_t now) const;

  void updateEyeTargets(uint32_t now);
  void updateEyeDynamics(float dt);

private:
  float idleX = 0.0f;
  float idleY = 0.0f;
  float happyTarget = 0.0f;
  float surpriseTarget = 0.0f;
  float sleepCloseTarget = 0.0f;
  float specialGlowTarget = 0.0f;

  uint32_t ringPulseStart = 0;

  AutonomousStateType recentAutonomous[3] = {AUTO_NONE, AUTO_NONE, AUTO_NONE};

  uint32_t chooseAutonomousDelayMs();
  void rememberAutonomous(uint8_t state);
  int applyAutonomousHistoryPenalty(uint8_t state, int weight);
  AutonomousStateType chooseAutonomousState(uint32_t now);
  uint32_t chooseAutonomousDurationMs(uint8_t state);
  void applyAutonomousEyeTargets(uint32_t now);
  void applyPetRequestEyeCue(uint32_t now);
  void applySleepRequestEyeCue(uint32_t now);
};

float smoothStep(float t);

extern EyesModel eyesModel;

#endif 