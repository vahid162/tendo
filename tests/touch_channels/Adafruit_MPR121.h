#pragma once
#include <cstdint>
#include "Wire.h"

constexpr uint8_t MPR121_ECR = 0x5E;
constexpr uint8_t MPR121_NHDT = 0x30;
constexpr uint8_t MPR121_NCLT = 0x31;
constexpr uint8_t MPR121_FDLT = 0x32;

struct Adafruit_MPR121 {
  inline static uint16_t mask = 0;
  inline static uint8_t address = 0, touchThreshold = 0, releaseThreshold = 0;
  inline static TestWire *bus = nullptr;
  inline static bool autoconfig = false;
  bool begin(uint8_t addr, TestWire *wire, uint8_t touch, uint8_t release, bool automatic) {
    address = addr; bus = wire; touchThreshold = touch;
    releaseThreshold = release; autoconfig = automatic;
    return true;
  }
  uint16_t touched() { return mask; }
  uint16_t baselineData(uint8_t) { return 1000; }
  uint16_t filteredData(uint8_t channel) { return 1000 - ((mask & (1U << channel)) ? 8 : 0); }
  uint8_t readRegister8(uint8_t) { return 0; }
};
