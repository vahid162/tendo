#include "../frame_delta.h"
#include "wokwi-api.h"
#include <stdio.h>

static int failures;
static uint16_t current[200 * 200], previous[200 * 200], screen[200 * 200];
static uint16_t other_previous[200 * 200];
#define CHECK(condition) do { if (!(condition)) { printf("FAIL line %d: %s\n", __LINE__, #condition); failures++; } } while (0)

static TandoFrameRegion update_in(const uint16_t *cached, TandoFrameRegion scan) {
  TandoFrameRegion r = tando_changed_region_in(current, cached, 200, 200, scan);
  CHECK(r.x + r.width <= 200 && r.y + r.height <= 200);
  CHECK(r.x % 2 == 0 && r.width % 2 == 0);
  for (unsigned y = r.y; y < r.y + r.height; y++) {
    memcpy(screen + y * 200 + r.x, current + y * 200 + r.x, r.width * sizeof(uint16_t));
    memcpy(previous + y * 200 + r.x, current + y * 200 + r.x, r.width * sizeof(uint16_t));
  }
  CHECK(memcmp(screen, current, sizeof(current)) == 0);
  CHECK(memcmp(previous, current, sizeof(current)) == 0);
  return r;
}

static TandoFrameRegion update(const uint16_t *cached) {
  TandoFrameRegion full = {0, 0, 200, 200};
  return update_in(cached, full);
}

void chip_init(void) {
  current[0] = 0x475f;
  TandoFrameRegion r = update(NULL);
  CHECK(r.width == 200 && r.height == 200);
  r = update(previous);
  CHECK(r.width == 0 && r.height == 0);
  current[199 * 200 + 199] = 0xffff;
  r = update(previous);
  CHECK(r.x == 198 && r.y == 199 && r.width == 2 && r.height == 1);
  current[199 * 200 + 199] = 0;
  current[3 * 200 + 7] = 0xf800;
  r = update(previous);
  CHECK(r.x == 6 && r.y == 3 && r.width == 194 && r.height == 197);
  CHECK(other_previous[3 * 200 + 7] == 0);
  r = tando_changed_region(current, other_previous, 200, 200);
  CHECK(r.width != 0 && r.height != 0);
  // A missing cache always falls back to a full image, even after prior updates.
  r = update(NULL);
  CHECK(r.width == 200 && r.height == 200);
  TandoFrameRegion empty = {0, 0, 0, 0};
  r = update_in(previous, empty);
  CHECK(r.width == 0 && r.height == 0);
  r = tando_changed_region_in(current, NULL, 200, 200, empty);
  CHECK(r.width == 200 && r.height == 200);
  current[3 * 200 + 7] ^= 0x07e0;
  TandoFrameRegion one = {7, 3, 1, 1};
  r = update_in(previous, one);
  CHECK(r.x == 6 && r.y == 3 && r.width == 2 && r.height == 1);
  current[199 * 200 + 199] ^= 0xffff;
  TandoFrameRegion clipped = {199, 199, 999, 999};
  r = update_in(previous, clipped);
  CHECK(r.x == 198 && r.y == 199 && r.width == 2 && r.height == 1);
  TandoFrameRegion outside = {201, 201, 1, 1};
  r = tando_changed_region_in(current, previous, 200, 200, outside);
  CHECK(r.width == 0 && r.height == 0);
  uint32_t seed = 0x562811;
  for (unsigned frame = 0; frame < 100; frame++) {
    for (unsigned i = 0; i < 31; i++) {
      seed = seed * 1664525u + 1013904223u;
      unsigned pixel = seed % (200 * 200);
      current[pixel] ^= (uint16_t)(seed >> 16) | 1u;
    }
    update(previous);
  }
  printf("FRAME_DELTA_TEST failures=%d\n", failures);
}

__attribute__((export_name("testResult"))) int test_result(void) { return failures; }
