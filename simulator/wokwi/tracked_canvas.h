#ifndef TANDO_TRACKED_CANVAS_H
#define TANDO_TRACKED_CANVAS_H

#include <Adafruit_GFX.h>
#include "frame_delta.h"

static inline TandoFrameRegion tando_union_region(TandoFrameRegion a, TandoFrameRegion b) {
  if (!a.width || !a.height) return b;
  if (!b.width || !b.height) return a;
  uint16_t x = a.x < b.x ? a.x : b.x;
  uint16_t y = a.y < b.y ? a.y : b.y;
  uint16_t right = a.x + a.width > b.x + b.width ? a.x + a.width : b.x + b.width;
  uint16_t bottom = a.y + a.height > b.y + b.height ? a.y + a.height : b.y + b.height;
  TandoFrameRegion region = {x, y, (uint16_t)(right - x), (uint16_t)(bottom - y)};
  return region;
}

struct TandoRingBackgroundKey {
  bool visible;
  uint8_t credits, thickness;
  uint16_t color;

  bool operator==(const TandoRingBackgroundKey &other) const {
    return visible == other.visible && credits == other.credits &&
           thickness == other.thickness && color == other.color;
  }
};

struct TandoEyeRenderState {
  bool keyValid = false;
  TandoRingBackgroundKey key = {};
  TandoFrameRegion dynamic = {0, 0, 0, 0};
};

class TandoTrackedCanvas : public GFXcanvas16 {
 public:
  TandoTrackedCanvas(uint16_t width, uint16_t height) : GFXcanvas16(width, height) {}

  void beginTracking() {
    region_ = {0, 0, 0, 0};
    tracking_ = getRotation() == 0;
  }

  TandoFrameRegion endTracking() {
    tracking_ = false;
    return region_;
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (tracking_) record(x, y, x, y);
    GFXcanvas16::drawPixel(x, y, color);
  }

  void drawFastVLine(int16_t x, int16_t y, int16_t height, uint16_t color) override {
    int32_t start = y, length = height;
    if (length < 0) { length = -length; start -= length - 1; }
    if (tracking_ && length) record(x, start, x, start + length - 1);
    GFXcanvas16::drawFastVLine(x, y, height, color);
  }

  void drawFastHLine(int16_t x, int16_t y, int16_t width, uint16_t color) override {
    int32_t start = x, length = width;
    if (length < 0) { length = -length; start -= length - 1; }
    if (tracking_ && length) record(start, y, start + length - 1, y);
    GFXcanvas16::drawFastHLine(x, y, width, color);
  }

 private:
  bool tracking_ = false;
  TandoFrameRegion region_ = {0, 0, 0, 0};

  void record(int32_t x, int32_t y, int32_t right, int32_t bottom) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right >= width()) right = width() - 1;
    if (bottom >= height()) bottom = height() - 1;
    if (right < x || bottom < y) return;
    TandoFrameRegion region = {(uint16_t)x, (uint16_t)y,
                              (uint16_t)(right - x + 1), (uint16_t)(bottom - y + 1)};
    region_ = tando_union_region(region_, region);
  }
};

#endif
