#include "tracked_canvas.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#define TANDO_WOKWI_MODE 1
uint32_t micros() { return 0; }
uint32_t millis() { return 0; }
struct SerialStub { template<typename... Args> void printf(const char *, Args...) {} } Serial;
struct EspStub { uint32_t getFreeHeap() { return 0; } } ESP;
/*FRAME_GLOBALS*/
/*STATE*/
constexpr unsigned PIXELS = FRAME_W * FRAME_H;
constexpr unsigned BYTES = PIXELS * sizeof(uint16_t);
void require(bool ok, const char *what) {
  if (!ok) { std::printf("FAIL %s\n", what); std::exit(1); }
}
// Model only the display's address window and successive pixel writes.
struct Adafruit_GC9A01A {
  uint16_t pixels[PIXELS] = {};
  unsigned x = 0, y = 0, w = 0, h = 0, cursor = 0;
  TandoFrameRegion sent = {};
  void startWrite() {}
  void setAddrWindow(unsigned xx, unsigned yy, unsigned ww, unsigned hh) {
    x = xx - FRAME_X; y = yy - FRAME_Y; w = ww; h = hh; cursor = 0;
    require(w && h && x + w <= FRAME_W && y + h <= FRAME_H, "display window bounds");
    sent = {(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h};
  }
  void writePixels(uint16_t *data, unsigned count) {
    require(cursor + count <= w * h, "display write length");
    for (unsigned i = 0; i < count; i++, cursor++)
      pixels[(y + cursor / w) * FRAME_W + x + cursor % w] = data[i];
  }
  void endWrite() { require(cursor == w * h, "incomplete display window"); }
} leftDisplay, rightDisplay;
GFXcanvas16 baselineFrame(FRAME_W, FRAME_H);
void baseline_pushLeft() {}
void baseline_pushRight() {}
/*FUNCTIONS*/
void palette() {
/*PALETTE*/
}
alignas(4) uint16_t oldFrames[2][PIXELS], backgroundMemory[2][PIXELS / 2];
unsigned imageCount = 0;

// Independent scalar oracle: do not use the production ROI finder here.
TandoFrameRegion expectedRegion(const uint16_t *current, const uint16_t *previous) {
  if (!previous) return {0, 0, FRAME_W, FRAME_H};
  unsigned left = FRAME_W, top = FRAME_H, right = 0, bottom = 0;
  for (unsigned y = 0; y < FRAME_H; y++) for (unsigned x = 0; x < FRAME_W; x++) {
    if (current[y * FRAME_W + x] == previous[y * FRAME_W + x]) continue;
    if (x < left) left = x;
    if (y < top) top = y;
    if (x + 1 > right) right = x + 1;
    bottom = y + 1;
  }
  if (!right) return {};
  left &= ~1u; right = (right + 1) & ~1u;
  if (right > FRAME_W) right = FRAME_W;
  return {(uint16_t)left, (uint16_t)top, (uint16_t)(right - left), (uint16_t)(bottom - top)};
}
bool equal(TandoFrameRegion a, TandoFrameRegion b) {
  return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
void reset(int fallback) {
  palette(); frame.setRotation(0); baselineFrame.setRotation(0);
  leftEye = rightEye = {}; happyBlend = surpriseBlend = sleepClose = specialGlow = blinkAmount = 0;
  reaction = R_NONE; autonomousState = AUTO_NONE; autonomousDuration = 0;
  ringPulseStart = 0; currentStage = 1; visualCredits = 3;
  completionFlag = petRequestPromptActive = sleepRequestPending = hungerPromptActive = sleepTagPresent = false;
  renderBackground[0] = fallback == 1 || fallback == 4 ? nullptr : backgroundMemory[0];
  renderBackground[1] = fallback == 1 || fallback == 5 ? nullptr : backgroundMemory[1];
  renderBackgroundValid = leftFrameCacheValid = rightFrameCacheValid = false;
  leftFrameCache = fallback == 2 ? nullptr : oldFrames[0];
  rightFrameCache = fallback == 3 ? nullptr : oldFrames[1];
  lastCanvasDynamic = {}; renderDiffRegion = {0, 0, FRAME_W, FRAME_H};
  eyeRenderState[0] = {}; eyeRenderState[1] = {};
  std::memset(oldFrames, 0, sizeof(oldFrames));
  std::memset(leftDisplay.pixels, 0, BYTES); std::memset(rightDisplay.pixels, 0, BYTES);
}
uint32_t pose(int kind, int i) {
  uint32_t now = 10000 + i * 120;
  if (kind == 1) ringPulseStart = 10000;
  if (kind == 2) blinkAmount = i % 12 == 1 ? 1.0f : i % 12 == 2 ? 0.6f : 0;
  if (kind == 3) { petRequestPromptActive = true; petRequestPromptStart = 10000; }
  if (kind == 4) { reaction = R_PET; reactionStart = 10000; happyBlend = .7f; specialGlow = .3f; }
  if (kind == 5) sleepRequestPending = true;
  if (kind == 6) { reaction = R_SLEEP; reactionStart = 6000; sleepClose = 1; sleepTagPresent = true; }
  if (kind == 7) { hungerPromptActive = true; hungerPromptStart = 10000; }
  if (kind == 8) {
    reaction = (Reaction)((i / 11) % (R_COMPLETE + 1)); reactionStart = 10000;
    visualCredits = (i / 7) % 10; currentStage = 1 + (i / 13) % 3;
    ringPulseStart = i % 25 < 10 ? 10000 + (i / 25) * 3000 : 0; completionFlag = (i / 17) % 2;
    happyBlend = (i % 5) * .22f; surpriseBlend = (i % 7) * .15f;
    sleepClose = reaction == R_SLEEP ? .95f : 0; specialGlow = (i % 9) * .12f;
    blinkAmount = i % 13 == 0 ? 1 : 0;
    leftEye.x = (i % 9 - 4) * 8; rightEye.x = -leftEye.x;
    leftEye.y = (i % 7 - 3) * 5; rightEye.y = -leftEye.y;
    // Deliberately stale overlays must remain hidden during every reaction.
    hungerPromptActive = petRequestPromptActive = sleepRequestPending = true;
  }
  if (kind == 9) { // Force ring overlap and off-canvas clipping.
    leftEye.x = (i % 3 - 1) * 75; rightEye.x = -leftEye.x;
    leftEye.y = (i % 5 - 2) * 40; rightEye.y = -leftEye.y;
    surpriseBlend = specialGlow = 1; happyBlend = .8f; visualCredits = i % 10; ringPulseStart = 10000;
  }
  if (kind == 10) {
    bool enabled = i % 9 >= 3;
    renderBackground[0] = enabled ? backgroundMemory[0] : nullptr;
    renderBackground[1] = enabled ? backgroundMemory[1] : nullptr;
    frame.setRotation(i % 17 == 0 ? 1 : 0); baselineFrame.setRotation(frame.getRotation());
    leftEye.x = (i % 7 - 3) * 12; rightEye.x = -leftEye.x;
  }
  if (kind == 11) {
    currentStage = 1 + i / 1010; visualCredits = (i / 101) % 10;
    int step = i % 101; ringPulseStart = step ? 10000 : 0;
    now = 10000 + step * RING_PULSE_MS / 100;
  }
  if (kind == 12) {
    autonomousState = i < 21 ? AUTO_WINK : AUTO_PLAY; autonomousVariant = 2;
    autonomousStart = i < 21 ? 10000 : 12520; autonomousDuration = 1800;
    autonomousWinkLeft = i % 2;
  }
  if (kind == 13) { // Unsupported palettes must use the complete renderer.
    palette(); if (i % 3 == 0) C_BLACK = 0x1234;
    if (i % 3 == 1) C_RING_TRACK = 0;
    if (i % 3 == 2) C_RING_S1 = 0;
  }
  return now;
}
void scenario(int kind, const char *name, int count, int fallback = 0) {
  reset(fallback);
  for (int i = 0; i < count; i++) {
    uint32_t now = pose(kind, i);
    for (int eye = 0; eye < 2; eye++) {
      bool left = eye == 0; baseline_renderDisplay(left, now);
      bool valid = left ? leftFrameCacheValid : rightFrameCacheValid;
      uint16_t *previous = left ? leftFrameCache : rightFrameCache;
      auto expected = expectedRegion(baselineFrame.getBuffer(), valid ? previous : nullptr);
      auto &display = left ? leftDisplay : rightDisplay; display.sent = {};
      renderDisplay(left, now);
      bool pixels = std::memcmp(baselineFrame.getBuffer(), frame.getBuffer(), BYTES) == 0;
      bool screen = std::memcmp(baselineFrame.getBuffer(), display.pixels, BYTES) == 0;
      if (!pixels || !screen || !equal(expected, display.sent)) {
        std::printf("FAIL %s frame=%d eye=%d fallback=%d canvas=%d display=%d roi=%d\n",
                    name, i, eye, fallback, pixels, screen, equal(expected, display.sent));
        std::exit(1);
      }
      imageCount++;
    }
  }
  std::printf("PASS %s images=%d fallback=%d\n", name, count * 2, fallback);
}
void primitives() {
  frame.setRotation(0); baselineFrame.setRotation(0);
  unsigned cases = 0;
  for (int kind = 0; kind < 3; kind++)
    for (int x : {-240, -1, 0, 12, 199, 220}) for (int y : {-240, -1, 0, 12, 199, 220})
      for (int length : {-240, -10, -1, 0, 1, 10, 240}) {
        baselineFrame.fillScreen(0); frame.fillScreen(0); frame.beginTracking();
        if (kind == 0) { baselineFrame.drawPixel(x, y, 1); frame.drawPixel(x, y, 1); }
        if (kind == 1) { baselineFrame.drawFastHLine(x, y, length, 1); frame.drawFastHLine(x, y, length, 1); }
        if (kind == 2) { baselineFrame.drawFastVLine(x, y, length, 1); frame.drawFastVLine(x, y, length, 1); }
        auto region = frame.endTracking();
        require(std::memcmp(baselineFrame.getBuffer(), frame.getBuffer(), BYTES) == 0, "primitive pixels");
        for (unsigned yy = 0; yy < FRAME_H; yy++) for (unsigned xx = 0; xx < FRAME_W; xx++)
          if (frame.getBuffer()[yy * FRAME_W + xx])
            require(xx >= region.x && xx < region.x + region.width && yy >= region.y && yy < region.y + region.height, "primitive ROI coverage");
        cases++;
      }
  std::printf("PASS tracked primitives=%u\n", cases);
}
int main() {
  initProgressRingGeometry(); primitives();
  scenario(0, "idle", 31); scenario(1, "pulse", 10); scenario(2, "blink", 25);
  scenario(3, "pet_request", 81); scenario(4, "pet_pose", 41);
  scenario(5, "sleep_request", 31); scenario(6, "persistent_sleep_Z", 41);
  scenario(7, "hunger", 31); scenario(8, "reactions_stages_credits", 161);
  scenario(9, "ring_overlap_clipping", 31); scenario(10, "background_rotation_transitions", 61);
  scenario(11, "all_stages_credits_pulse_samples", 3030); scenario(12, "wink_play", 41);
  scenario(13, "palette_fallback", 31);
  for (int fallback = 1; fallback <= 5; fallback++) scenario(8, "allocation_fallback", 41, fallback);
  std::printf("PASS renderer exact images=%u; no simulator timing or hardware claims\n", imageCount);
}
