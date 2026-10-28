#ifndef TANDO_FRAME_DELTA_H
#define TANDO_FRAME_DELTA_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct {
  uint16_t x, y, width, height;
} TandoFrameRegion;

// Read pixel pairs without violating uint16_t buffer aliasing rules. Call only
// after checking both buffers and the row stride are 32-bit aligned.
static inline int tando_frame_pair_equal(const uint16_t *a, const uint16_t *b) {
  uint32_t first, second;
  memcpy(&first, __builtin_assume_aligned(a, 4), sizeof(first));
  memcpy(&second, __builtin_assume_aligned(b, 4), sizeof(second));
  return first == second;
}

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
  // Expand to pixel pairs so the fast scan can use aligned word loads.
  scan.x &= (uint16_t)~1u;
  scan_right = (scan_right + 1u) & ~1u;
  if (scan_right > width) scan_right = width;
  const int paired = !(width & 1u) && !((uintptr_t)current & 3u) &&
                      !((uintptr_t)previous & 3u);
  uint16_t min_x = width, min_y = height, max_x = 0, max_y = 0;
  for (uint16_t y = scan.y; y < scan_bottom; y++) {
    const uint16_t *row = current + (size_t)y * width;
    const uint16_t *old_row = previous + (size_t)y * width;
    uint16_t left = scan.x, right = scan_right;
    if (paired) {
      while (left < right && tando_frame_pair_equal(row + left, old_row + left)) left += 2;
      if (left == right) continue;
      while (tando_frame_pair_equal(row + right - 2, old_row + right - 2)) right -= 2;
    } else {
      if (memcmp(row + left, old_row + left, (right - left) * sizeof(uint16_t)) == 0) continue;
      while (row[left] == old_row[left]) left++;
      while (row[right - 1] == old_row[right - 1]) right--;
    }
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

// A fixed plan avoids allocations and bounds extra display address windows.
typedef struct {
  TandoFrameRegion regions[4];
  uint8_t count;
} TandoFramePlan;

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

// Scan each row once using the same clipping and pair alignment as the bbox
// helper. Nearby changed rows share a window; too many bands use the old bbox.
static inline TandoFramePlan tando_changed_bands_in(const uint16_t *current,
    const uint16_t *previous, uint16_t width, uint16_t height, TandoFrameRegion scan) {
  TandoFramePlan plan = {0};
  if (!previous) {
    TandoFrameRegion full = {0, 0, width, height};
    if (width && height) { plan.regions[0] = full; plan.count = 1; }
    return plan;
  }
  uint32_t bottom = (uint32_t)scan.y + scan.height;
  if (bottom > height) bottom = height;
  TandoFrameRegion bounds = {0, 0, 0, 0};
  uint8_t overflow = 0;
  for (uint16_t y = scan.y; y < bottom; y++) {
    TandoFrameRegion row_scan = {scan.x, y, scan.width, 1};
    TandoFrameRegion row = tando_changed_region_in(current, previous, width, height, row_scan);
    if (!row.width) continue;
    bounds = tando_union_region(bounds, row);
    if (overflow) continue;
    if (plan.count && y - (plan.regions[plan.count - 1].y +
                          plan.regions[plan.count - 1].height) <= 4) {
      plan.regions[plan.count - 1] = tando_union_region(plan.regions[plan.count - 1], row);
    } else if (plan.count < 4) {
      plan.regions[plan.count++] = row;
    } else {
      overflow = 1;
    }
  }
  if (overflow) { plan.regions[0] = bounds; plan.count = 1; }
  return plan;
}

#endif
