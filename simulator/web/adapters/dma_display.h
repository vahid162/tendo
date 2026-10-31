#pragma once
#include <stdint.h>
#include "SPI.h"
// Browser preview bypasses the ESP peripheral; canonical renderer uses its
// existing legacy-display API, backed by direct browser framebuffers.
class TandoDmaDisplay {
 public:
  TandoDmaDisplay(int, int, int, int, int, int) {}
  bool active() const { return false; }
  bool begin(SPIClass &) { return false; }
  bool writeRegion(bool, const uint16_t *, uint16_t, uint16_t, uint16_t,
                   uint16_t, uint16_t) {
    return false;
  }
};
