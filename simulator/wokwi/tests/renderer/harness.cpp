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
  uint8_t visited[PIXELS] = {};
  unsigned x = 0, y = 0, w = 0, h = 0, cursor = 0;
  unsigned windows = 0, calls = 0, bytes = 0;
  bool writing = false;
  TandoFrameRegion sent = {};
  void resetWrites() {
    require(!writing, "unclosed display transaction");
    windows = calls = bytes = 0; sent = {};
    std::memset(visited, 0, sizeof(visited));
  }
  void startWrite() { require(!writing, "nested display transaction"); writing = true; }
  void setAddrWindow(unsigned xx, unsigned yy, unsigned ww, unsigned hh) {
    require(writing, "window outside display transaction");
    if (windows) require(cursor == w * h, "incomplete prior display window");
    x = xx - FRAME_X; y = yy - FRAME_Y; w = ww; h = hh; cursor = 0;
    require(w && h && x + w <= FRAME_W && y + h <= FRAME_H, "display window bounds");
    require(x % 2 == 0 && w % 2 == 0, "display pair alignment");
    require(++windows <= 4, "display window cap");
    unsigned left = sent.width && sent.x < x ? sent.x : x;
    unsigned top = sent.height && sent.y < y ? sent.y : y;
    unsigned right = sent.x + sent.width > x + w ? sent.x + sent.width : x + w;
    unsigned bottom = sent.y + sent.height > y + h ? sent.y + sent.height : y + h;
    sent = {(uint16_t)left, (uint16_t)top, (uint16_t)(right - left), (uint16_t)(bottom - top)};
  }
  void writePixels(uint16_t *data, unsigned count) {
    require(writing && count && cursor + count <= w * h, "display write length");
    calls++; bytes += count * sizeof(uint16_t);
    for (unsigned i = 0; i < count; i++, cursor++) {
      unsigned offset = (y + cursor / w) * FRAME_W + x + cursor % w;
      require(!visited[offset], "overlapping display windows"); visited[offset] = 1;
      require(data[i] == frame.getBuffer()[offset], "packed display pixel");
      pixels[offset] = data[i];
    }
  }
  void endWrite() {
    require(writing && cursor == w * h, "incomplete display window"); writing = false;
  }
} leftDisplay, rightDisplay;
GFXcanvas16 baselineFrame(FRAME_W, FRAME_H);
void baseline_pushLeft() {}
void baseline_pushRight() {}
uint64_t ringOverlayChecks = 0;
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
uint32_t pose(int kind, int i, uint32_t interval) {
  uint32_t now = 10000 + i * interval;
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
  if (kind == 14) {
    toggleSimulatorTransferMode();
    petRequestPromptActive = true; petRequestPromptStart = 10000;
    leftEye.x = (i % 7 - 3) * 15; rightEye.x = -leftEye.x;
  }
  return now;
}
void scenario(int kind, const char *name, int count, int fallback = 0, uint32_t interval = 120) {
  reset(fallback);
  uint64_t sentBytes = 0, sentCalls = 0, bboxBytes = 0, bboxCalls = 0;
  for (int i = 0; i < count; i++) {
    uint32_t now = pose(kind, i, interval);
    for (int eye = 0; eye < 2; eye++) {
      bool left = eye == 0; baseline_renderDisplay(left, now);
      bool valid = left ? leftFrameCacheValid : rightFrameCacheValid;
      uint16_t *previous = left ? leftFrameCache : rightFrameCache;
      auto expected = expectedRegion(baselineFrame.getBuffer(), valid ? previous : nullptr);
      require(equal(expected, tando_changed_region(baselineFrame.getBuffer(),
                    valid ? previous : nullptr, FRAME_W, FRAME_H)), "bbox scalar oracle");
      auto &display = left ? leftDisplay : rightDisplay; display.resetWrites();
      ringOverlayChecks = 0;
      renderDisplay(left, now);
      if (!simulatorUseBands) require(display.windows <= 1, "bbox uses one display window");
      if (kind == 0 && !fallback)
        require(ringOverlayChecks == 0, "idle must not scan empty ring background");
      bool pixels = std::memcmp(baselineFrame.getBuffer(), frame.getBuffer(), BYTES) == 0;
      bool screen = std::memcmp(baselineFrame.getBuffer(), display.pixels, BYTES) == 0;
      if (previous)
        require(std::memcmp(previous, baselineFrame.getBuffer(), BYTES) == 0, "per-eye cache pixels");
      require(display.bytes <= expected.width * expected.height * sizeof(uint16_t), "transfer exceeds bbox");
      if (!pixels || !screen || !equal(expected, display.sent)) {
        std::printf("FAIL %s frame=%d eye=%d fallback=%d canvas=%d display=%d roi=%d\n",
                    name, i, eye, fallback, pixels, screen, equal(expected, display.sent));
        std::exit(1);
      }
      imageCount++;
      if (i && valid) {
        sentBytes += display.bytes; sentCalls += display.calls;
        bboxBytes += expected.width * expected.height * sizeof(uint16_t);
        bboxCalls += !expected.width ? 0 : expected.width == FRAME_W ? 1 : expected.height;
      }
    }
  }
  if (kind == 3 && !fallback && interval == 120 && simulatorUseBands) {
    require(sentBytes * 10 <= bboxBytes * 7, "PET transfer must save at least 30 percent payload");
    require(sentCalls * 5 <= bboxCalls, "PET transfer must save at least 80 percent writes");
    std::printf("PASS PET transfer bytes=%llu bbox_bytes=%llu calls=%llu bbox_calls=%llu\n",
                (unsigned long long)sentBytes, (unsigned long long)bboxBytes,
                (unsigned long long)sentCalls, (unsigned long long)bboxCalls);
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
void rectangleEdges() {
  struct CountingCanvas : TandoTrackedCanvas {
    using TandoTrackedCanvas::TandoTrackedCanvas;
    unsigned verticalCalls = 0;
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
      verticalCalls++;
      TandoTrackedCanvas::drawFastVLine(x, y, h, color);
    }
  } cost(37, 23);
  cost.fillRect(3, 2, 30, 17, 0x475f);
  require(cost.verticalCalls == 0, "filled rectangle must avoid per-column virtual calls");
  TandoTrackedCanvas actual(31, 19);
  GFXcanvas16 expected(31, 19);
  unsigned cases = 0;
  for (int rotation = 0; rotation < 4; rotation++) {
    actual.setRotation(rotation); expected.setRotation(rotation);
    for (int x : {-40,-1,0,1,15,18,30,40}) for (int y : {-40,-1,0,1,9,18,30,40})
      for (int w : {-40,-2,-1,0,1,2,15,31,40}) for (int h : {-40,-2,-1,0,1,2,9,19,40}) {
        actual.fillScreen(0); expected.fillScreen(0); actual.beginTracking();
        actual.fillRect(x, y, w, h, 0x9234); expected.fillRect(x, y, w, h, 0x9234);
        auto got = actual.endTracking();
        require(std::memcmp(actual.getBuffer(), expected.getBuffer(), 31 * 19 * sizeof(uint16_t)) == 0,
                "rectangle edge pixels");
        TandoFrameRegion want = {};
        if (!rotation) {
          unsigned left = 31, top = 19, right = 0, bottom = 0;
          for (unsigned yy = 0; yy < 19; yy++) for (unsigned xx = 0; xx < 31; xx++) {
            if (!expected.getBuffer()[yy * 31 + xx]) continue;
            if (xx < left) left = xx;
            if (yy < top) top = yy;
            if (xx + 1 > right) right = xx + 1;
            if (yy + 1 > bottom) bottom = yy + 1;
          }
          if (right) want = {(uint16_t)left, (uint16_t)top,
                            (uint16_t)(right - left), (uint16_t)(bottom - top)};
        }
        require(equal(got, want), "rectangle exact ROI or rotation fallback");
        cases++;
      }
  }
  std::printf("PASS rectangle edge cases=%u\n", cases);
}
void roundedEdges() {
  struct CountingCanvas : TandoTrackedCanvas {
    using TandoTrackedCanvas::TandoTrackedCanvas;
    unsigned verticalCalls = 0;
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
      verticalCalls++;
      TandoTrackedCanvas::drawFastVLine(x, y, h, color);
    }
  } cost(200, 200);
  cost.fillRoundRect(47, 57, 106, 78, 25, 0x475f);
  require(cost.verticalCalls == 0, "rounded eye corners must avoid strided column fills");
  cost.beginTracking();
  cost.fillRoundRect(47, 57, 106, 78, 25, 0x475f);
  cost.drawPixel(0, 0, 1);
  auto tracked = cost.endTracking();
  require(tracked.x == 0 && tracked.y == 0 && tracked.width >= 153 && tracked.height >= 135,
          "rounded fill must restore tracking for subsequent graphics");
  TandoTrackedCanvas actual(31,19);
  GFXcanvas16 expected(31,19);
  constexpr unsigned count=31*19;
  unsigned cases=0;
  for(int rot=0;rot<4;rot++) {
    actual.setRotation(rot);expected.setRotation(rot);
    for(int x:{-40,-1,0,1,15,18,30,40}) for(int y:{-40,-1,0,1,9,18,30,40})
      for(int w:{-2,0,1,2,3,4,5,8,15,31,40}) for(int h:{-2,0,1,2,3,4,5,8,9,19,40}) for(int r:{-1,0,1,2,3,4,8,15,21,30,50}) {
        actual.fillScreen(0);expected.fillScreen(0);actual.beginTracking();
        actual.fillRoundRect(x,y,w,h,r,0x9234);expected.fillRoundRect(x,y,w,h,r,0x9234);
        auto got=actual.endTracking();
        if(std::memcmp(actual.getBuffer(),expected.getBuffer(),count*2)) {
          std::printf("FAIL rounded rot=%d x=%d y=%d w=%d h=%d r=%d\n",rot,x,y,w,h,r);std::exit(1);
        }
        if(!rot) {
          for(unsigned yy=0;yy<19;yy++)for(unsigned xx=0;xx<31;xx++)if(expected.getBuffer()[yy*31+xx])
            require(xx>=got.x && xx<got.x+got.width && yy>=got.y && yy<got.y+got.height,"rounded ROI covers pixels");
        } else require(!got.width && !got.height,"rotated rounded tracking fallback");
        cases++;
      }
  }
  std::printf("PASS rounded edges=%u\n",cases);
}

namespace SimulatorLoopFixture {
struct Counters { unsigned loops, serial, demo, blink, targets, dynamics, renders, waits, delays, begin, end; } count;
uint32_t clockNow, lastFrame, previousFrameTime, simulatorFrameIntervalMs = 60;
uint32_t nextIdleLook = 1000000, lastBlink, lastTarget;
int reaction = 0, autonomousState = 0;
constexpr int R_NONE = 0, AUTO_NONE = 0;
bool sleepRequestPending = false;
float lastDt;
uint32_t millis() { return clockNow; }
void noteSimulatorLoop(uint32_t) { count.loops++; }
void updateSerial(uint32_t) { count.serial++; }
void updateDemoClock(uint32_t) { count.demo++; }
void updateBlink(uint32_t now) { count.blink++; lastBlink = now; }
void updateEyeTargets(uint32_t now) {
  require(lastBlink == now, "targets must see current blink state");
  count.targets++; lastTarget = now;
}
void updateEyeDynamics(float dt) {
  require(lastTarget == clockNow, "dynamics must consume current frame targets");
  count.dynamics++; lastDt = dt;
}
void renderDisplay(bool left, uint32_t now) {
  require(left == (count.renders % 2 == 0) && now == clockNow, "loop eye order/time");
  count.renders++;
}
void noteSimulatorWait() { count.waits++; }
void delay(unsigned ms) { require(ms == 1, "simulator wait must yield"); count.delays++; }
void beginSimulatorFrame() { count.begin++; }
void endSimulatorFrame() { count.end++; }
void updateReaction(uint32_t) {}
void servicePendingSystemReaction(uint32_t) {}
void servicePendingUserReaction(uint32_t) {}
void updateSleepNeedState(uint32_t) {}
void updateHungerRequestScheduler(uint32_t) {}
void updatePetRequestScheduler(uint32_t) {}
void updateAutonomous(uint32_t) {}
void chooseIdleLook(uint32_t) {}
void updateReactionLed(uint32_t) {}
/*SIM_LOOP*/
void step(uint32_t now) { clockNow = now; loop(); }
void services(unsigned expected) {
  require(count.loops == expected && count.serial == expected &&
          count.demo == expected && count.blink == expected, "waiting must service input/timers/blink");
}
void run() {
  for (uint32_t now = 1; now < 60; now++) step(now);
  services(59);
  require(count.targets == 0 && count.dynamics == 0 && count.renders == 0,
          "waiting must not recompute visual targets");
  require(count.waits == 59 && count.delays == 59, "waiting must yield each pass");
  step(60); services(60);
  require(count.targets == 1 && count.dynamics == 1 && count.renders == 2 &&
          count.begin == 1 && count.end == 1, "60ms must admit one complete frame pair");
  require(lastDt > .0599f && lastDt < .0601f, "frame dynamics interval");
  simulatorFrameIntervalMs = 120;
  step(179); services(61);
  require(count.targets == 1 && count.renders == 2, "120ms target must keep 179ms waiting");
  step(180); services(62);
  require(count.targets == 2 && count.dynamics == 2 && count.renders == 4 &&
          count.begin == 2 && count.end == 2, "120ms target must admit 180ms");
  require(lastDt == .10f && count.waits == 60 && count.delays == 60, "dt clamp and wait budget");
  count = {}; simulatorFrameIntervalMs = 60;
  lastFrame = previousFrameTime = UINT32_MAX - 30;
  step(28); services(1);
  require(count.targets == 0 && count.renders == 0, "rollover 59ms must wait");
  step(29); services(2);
  require(count.targets == 1 && count.dynamics == 1 && count.renders == 2 &&
          count.waits == 1 && count.delays == 1, "rollover 60ms must admit");
  std::printf("PASS actual simulator loop frame/target/service budgets\n");
}
}

int main() {
  SimulatorLoopFixture::run();
  initProgressRingGeometry(); primitives(); rectangleEdges(); roundedEdges();
  require(simulatorFrameIntervalMs == FRAME_MS && simulatorUseBands, "simulator default settings");
  toggleSimulatorFrameInterval();
  require(simulatorFrameIntervalMs == 120, "slower simulator frame target");
  toggleSimulatorFrameInterval();
  require(simulatorFrameIntervalMs == FRAME_MS, "restore default simulator frame target");
  toggleSimulatorTransferMode();
  require(!simulatorUseBands, "select bbox transfer");
  scenario(3, "pet_request_bbox", 81);
  scenario(8, "reactions_bbox", 161);
  scenario(10, "background_transitions_bbox", 61);
  toggleSimulatorTransferMode();
  require(simulatorUseBands, "restore bands transfer");
  scenario(0, "idle", 31); scenario(1, "pulse", 10); scenario(2, "blink", 25);
  scenario(3, "pet_request", 81); scenario(4, "pet_pose", 41);
  scenario(3, "pet_request_60ms", 161, 0, 60);
  scenario(5, "sleep_request", 31); scenario(6, "persistent_sleep_Z", 41);
  scenario(7, "hunger", 31); scenario(8, "reactions_stages_credits", 161);
  scenario(9, "ring_overlap_clipping", 31); scenario(10, "background_rotation_transitions", 61);
  scenario(11, "all_stages_credits_pulse_samples", 3030); scenario(12, "wink_play", 41);
  scenario(13, "palette_fallback", 31);
  for (int fallback = 1; fallback <= 5; fallback++) scenario(8, "allocation_fallback", 41, fallback);
  scenario(14, "transfer_mode_switches", 61);
  std::printf("PASS renderer exact images=%u; no simulator timing or hardware claims\n", imageCount);
}
