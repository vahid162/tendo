#ifndef TANDO_FRAME_DELTA_H
#define TANDO_FRAME_DELTA_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct {
  uint16_t x, y, width, height;
} TandoFrameRegion;

// The canvas width is even, so pairs of RGB565 pixels are 32-bit aligned.
static inline TandoFrameRegion tando_changed_region_in(const uint16_t *current,
    const uint16_t *previous, uint16_t width, uint16_t height, TandoFrameRegion scan) {
  TandoFrameRegion region = {0, 0, width, height};
  if (!previous) return region;
  uint32_t scan_right = (uint32_t)scan.x + scan.width;
  uint32_t scan_bottom = (uint32_t)scan.y + scan.height;
  if (scan_right > width) scan_right = width;
  if (scan_bottom > height) scan_bottom = height;
  if (scan.x >= scan_right || scan.y >= scan_bottom) {
    TandoFrameRegion empty = {0, 0, 0, 0};
    return empty;
  }
  // An aligned superset keeps the ESP32 ROM comparison on its word path.
  scan.x &= (uint16_t)~1u;
  scan_right = (scan_right + 1u) & ~1u;
  if (scan_right > width) scan_right = width;
  uint16_t min_x = width, min_y = height, max_x = 0, max_y = 0;
  for (uint16_t y = scan.y; y < scan_bottom; y++) {
    const uint16_t *row = current + (size_t)y * width;
    const uint16_t *old_row = previous + (size_t)y * width;
    if (memcmp(row + scan.x, old_row + scan.x,
               (scan_right - scan.x) * sizeof(uint16_t)) == 0) continue;
    uint16_t left = scan.x, right = scan_right;
    while (row[left] == old_row[left]) left++;
    while (row[right - 1] == old_row[right - 1]) right--;
    if (left < min_x) min_x = left;
    if (right > max_x) max_x = right;
    if (y < min_y) min_y = y;
    max_y = y + 1;
  }
  if (min_x == width) {
    TandoFrameRegion empty = {0, 0, 0, 0};
    return empty;
  }
  min_x &= (uint16_t)~1u;
  max_x = (max_x + 1u) & ~1u;
  if (max_x > width) max_x = width;
  region.x = min_x;
  region.y = min_y;
  region.width = max_x - min_x;
  region.height = max_y - min_y;
  return region;
}

static inline TandoFrameRegion tando_changed_region(const uint16_t *current,
    const uint16_t *previous, uint16_t width, uint16_t height) {
  TandoFrameRegion scan = {0, 0, width, height};
  return tando_changed_region_in(current, previous, width, height, scan);
}

#endif
