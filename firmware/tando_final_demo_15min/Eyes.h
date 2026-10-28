#pragma once

#include "TandoTypes.h"

// Reaction handoff shares animation state; displays and the canvas stay private.
extern uint32_t nextIdleLook;
extern BlinkState blinkState;
extern float blinkAmount;
extern uint32_t blinkStart;
extern uint32_t nextBlink;
extern AutonomousState autonomousState;

float smoothStep(float t);
void triggerRingPulse(uint32_t now);
void scheduleNextBlink(uint32_t now);
void startBlink(uint32_t now);
void chooseIdleLook(uint32_t now);
const char *autonomousStateName(uint8_t state);
void scheduleNextAutonomous(uint32_t now);
void cancelAutonomous();
void interruptAutonomousForInteraction(uint32_t now);
void startAutonomous(uint8_t state, uint32_t now);
void updateAutonomous(uint32_t now);
void updateBlink(uint32_t now);
void updateEyeTargets(uint32_t now);
void updateEyeDynamics(float dt);
void renderDisplay(bool leftSide, uint32_t now);
void setupEyes();

#if TANDO_WOKWI_MODE
// Optional diagnostics for virtual-time simulation; absent from hardware builds.
void printSimProbe();
void setupSimulatorCaches();
void noteSimulatorLoop(uint32_t now);
void noteSimulatorWait();
void beginSimulatorFrame();
void endSimulatorFrame();
#endif
