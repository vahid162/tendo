#pragma once
#include "Arduino.h"
constexpr int HSPI = 1;
class SPIClass {
 public:
  explicit SPIClass(int) {}
  void begin(int, int, int, int) {}
  void end() {}
  uint32_t getClockDivider() const { return 0; }
};
