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


// Independent scalar oracle for scan clipping; production helpers are not used.
static TandoFrameRegion expected_in(const uint16_t *cached, TandoFrameRegion scan) {
  TandoFrameRegion full = {0, 0, 200, 200};
  if (!cached) return full;
  uint32_t right = (uint32_t)scan.x + scan.width, bottom = (uint32_t)scan.y + scan.height;
  if (right > 200) right = 200;
  if (bottom > 200) bottom = 200;
  TandoFrameRegion result = {0, 0, 0, 0};
  if (scan.x >= right || scan.y >= bottom) return result;
  unsigned begin = scan.x & ~1u; right = (right + 1u) & ~1u;
  if (right > 200) right = 200;
  unsigned left = 200, top = 200, end = 0, last = 0;
  for (unsigned y = scan.y; y < bottom; y++) for (unsigned x = begin; x < right; x++) {
    if (current[y * 200 + x] == cached[y * 200 + x]) continue;
    if (x < left) left = x;
    if (y < top) top = y;
    if (x + 1 > end) end = x + 1;
    if (y + 1 > last) last = y + 1;
  }
  if (!end) return result;
  left &= ~1u; end = (end + 1u) & ~1u;
  result.x = left; result.y = top; result.width = end - left; result.height = last - top;
  return result;
}
static int same_region(TandoFrameRegion a, TandoFrameRegion b) {
  return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
// Exercise word-load alignment independently for both buffers, including an odd
// final pixel and odd row stride. Expected windows follow each changed pixel.
static void alignment_tests(void) {
  _Alignas(4) uint16_t a[32 * 3 + 1] = {0}, b[32 * 3 + 1] = {0};
  unsigned cases = 0;
  for (unsigned width = 31; width <= 32; width++) {
    TandoFrameRegion full = {0, 0, width, 3};
    for (unsigned offset_a = 0; offset_a < 2; offset_a++)
      for (unsigned offset_b = 0; offset_b < 2; offset_b++) {
        uint16_t *next = a + offset_a, *old = b + offset_b;
        for (unsigned pixel = 0; pixel < width * 3; pixel++) {
          unsigned x = pixel % width, left = x & ~1u, right = (x + 2u) & ~1u;
          if (right > width) right = width;
          TandoFrameRegion expected = {left, pixel / width, right - left, 1};
          next[pixel] = 0x9234;
          CHECK(same_region(tando_changed_region_in(next, old, width, 3, full), expected));
          TandoFramePlan plan = tando_changed_bands_in(next, old, width, 3, full);
          CHECK(plan.count == 1 && same_region(plan.regions[0], expected));
          next[pixel] = 0;
          CHECK(tando_changed_region_in(next, old, width, 3, full).width == 0);
          CHECK(tando_changed_bands_in(next, old, width, 3, full).count == 0);
          cases++;
        }
      }
  }
  printf("FRAME_ALIGNMENT_TEST cases=%u failures=%d\n", cases, failures);
}

static unsigned planner_cases;
static unsigned check_plan(const uint16_t *cached, TandoFrameRegion scan) {
  planner_cases++;
  TandoFramePlan plan = tando_changed_bands_in(current, cached, 200, 200, scan);
  TandoFrameRegion expected = expected_in(cached, scan), bounds = {0, 0, 0, 0};
  CHECK(same_region(tando_changed_region_in(current, cached, 200, 200, scan), expected));
  CHECK(plan.count <= 4);
  unsigned pixels = 0;
  if (cached) memcpy(screen, cached, sizeof(screen)); else memset(screen, 0, sizeof(screen));
  for (unsigned i = 0; i < plan.count; i++) {
    TandoFrameRegion r = plan.regions[i];
    CHECK(r.width && r.height && r.x + r.width <= 200 && r.y + r.height <= 200);
    CHECK(r.x % 2 == 0 && r.width % 2 == 0);
    if (i) CHECK(plan.regions[i - 1].y + plan.regions[i - 1].height <= r.y);
    if (!bounds.width) bounds = r;
    else {
      unsigned left = bounds.x < r.x ? bounds.x : r.x;
      unsigned right = bounds.x + bounds.width > r.x + r.width ? bounds.x + bounds.width : r.x + r.width;
      bounds.x = left; bounds.width = right - left; bounds.height = r.y + r.height - bounds.y;
    }
    pixels += r.width * r.height;
    for (unsigned y = r.y; y < r.y + r.height; y++) {
      memcpy(screen + y * 200 + r.x, current + y * 200 + r.x, r.width * sizeof(uint16_t));
    }
  }
  CHECK(same_region(bounds, expected));
  CHECK(pixels <= expected.width * expected.height);
  // Check coverage without trusting either production ROI finder or its union.
  uint32_t right = (uint32_t)scan.x + scan.width, bottom = (uint32_t)scan.y + scan.height;
  if (right > 200) right = 200; if (bottom > 200) bottom = 200;
  int scan_valid = scan.x < right && scan.y < bottom;
  unsigned begin = scan.x & ~1u; right = (right + 1u) & ~1u; if (right > 200) right = 200;
  for (unsigned y = 0; y < 200; y++) for (unsigned x = 0; x < 200; x++) {
    int covered = 0;
    for (unsigned i = 0; i < plan.count; i++) {
      TandoFrameRegion r = plan.regions[i];
      if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) covered++;
    }
    CHECK(covered <= 1);
    if (!cached || (scan_valid && x >= begin && x < right && y >= scan.y && y < bottom)) {
      if (!cached || current[y * 200 + x] != cached[y * 200 + x]) CHECK(covered == 1);
    }
    CHECK(screen[y * 200 + x] == (covered ? current[y * 200 + x] : cached[y * 200 + x]));
  }
  return plan.count;
}
static void planner_tests(void) {
  TandoFrameRegion full = {0, 0, 200, 200}, empty = {0, 0, 0, 0};
  memset(current, 0, sizeof(current)); memset(previous, 0, sizeof(previous));
  CHECK(check_plan(previous, full) == 0);
  CHECK(check_plan(previous, empty) == 0);
  CHECK(check_plan(NULL, empty) == 1);
  current[10 * 200 + 7] = 1; current[15 * 200 + 191] = 2;
  CHECK(check_plan(previous, full) == 1); // Four empty rows share one band.
  current[15 * 200 + 191] = 0; current[16 * 200 + 191] = 2;
  CHECK(check_plan(previous, full) == 2); // Five empty rows split.
  memset(current, 0, sizeof(current));
  for (unsigned i = 0; i < 4; i++) current[(1 + i * 6) * 200 + i * 40 + 1] = i + 1;
  CHECK(check_plan(previous, full) == 4);
  TandoFramePlan savings = tando_changed_bands_in(current, previous, 200, 200, full);
  unsigned area = 0;
  for (unsigned i = 0; i < savings.count; i++) area += savings.regions[i].width * savings.regions[i].height;
  CHECK(area == 8); // Four independent aligned pairs, versus a broad bbox.
  current[25 * 200 + 199] = 9;
  CHECK(check_plan(previous, full) == 1); // Fifth band falls back before sending.
  TandoFrameRegion one = {7, 3, 1, 1}, clipped = {199, 199, 65535, 65535};
  TandoFrameRegion outside = {201, 201, 65535, 65535};
  current[3 * 200 + 7] = 7; current[199 * 200 + 199] = 4;
  CHECK(check_plan(previous, one) == 1);
  CHECK(check_plan(previous, clipped) == 1);
  CHECK(check_plan(previous, outside) == 0);
  CHECK(check_plan(current, full) == 0);
  CHECK(other_previous[3 * 200 + 7] == 0);
  CHECK(check_plan(other_previous, full) != 0);
  uint32_t seed = 0xa1752d31u;
  for (unsigned iteration = 0; iteration < 250; iteration++) {
    memcpy(previous, current, sizeof(previous));
    for (unsigned j = 0; j < (iteration % 13 == 0 ? 40000u : 31u); j++) {
      seed = seed * 1664525u + 1013904223u;
      current[seed % 40000] ^= (uint16_t)(seed >> 16) | 1u;
    }
    check_plan(previous, full);
    TandoFrameRegion random_scan = {(uint16_t)(seed % 240), (uint16_t)((seed >> 8) % 240),
      (uint16_t)((seed >> 16) % 240), (uint16_t)((seed >> 24) % 240)};
    check_plan(previous, random_scan);
  }
  printf("FRAME_PLAN_TEST cases=%u failures=%d\n", planner_cases, failures);
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
  alignment_tests();
  planner_tests();
  printf("FRAME_DELTA_TEST failures=%d\n", failures);
}

__attribute__((export_name("testResult"))) int test_result(void) { return failures; }
