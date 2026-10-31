#pragma once
#include "SPI.h"
#include "firmware/tando_final_demo_15min/TandoConfig.h"

extern uint16_t previewPixels[2][240 * 240];

// Preserve the panel window and round mask while bypassing SPI transport.
class Adafruit_GC9A01A {
  int side_;
  unsigned x_ = 0, y_ = 0, w_ = 0, h_ = 0, cursor_ = 0;

 public:
  Adafruit_GC9A01A(SPIClass *, int, int cs, int)
      : side_(cs == LEFT_CS ? 0 : 1) {}

  void begin(uint32_t) {}
  void setRotation(int) {}
  void startWrite() {}
  void endWrite() {}

  void fillScreen(uint16_t color) {
    for (auto &pixel : previewPixels[side_]) pixel = color;
  }

  void setAddrWindow(unsigned x, unsigned y, unsigned w, unsigned h) {
    x_ = x;
    y_ = y;
    w_ = w;
    h_ = h;
    cursor_ = 0;
  }

  void writePixels(uint16_t *pixels, unsigned count) {
    if (!w_ || !h_) return;
    for (unsigned i = 0; i < count; i++, cursor_++) {
      unsigned x = x_ + cursor_ % w_;
      unsigned y = y_ + cursor_ / w_;
      if (x < 240 && y < 240 && cursor_ < w_ * h_) {
        int dx = int(x) - 120;
        int dy = int(y) - 120;
        previewPixels[side_][y * 240 + x] =
            dx * dx + dy * dy <= 14400 ? pixels[i] : 0;
      }
    }
  }
};
