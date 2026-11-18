#include "EyesView.h"
#include "../TandoConfig.h"
#include "../model/Demo.h"

#include "../model/Reaction.h"
#include "../model/Request.h"
#include "../controller/DemoController.h"
#include <math.h>

// Keeps display construction, the shared canvas, rendering, and eye-only motion together.
// (view half) display construction, the shared canvas, drawing and pushing frames.
// The eye motion state lives in model/EyesModel.cpp; this class only reads it.


// Display objects and the canvas used to be file-level statics (commented originals below).
// They are members of EyesView now, built here in declaration order.
EyesView::EyesView(const EyesModel &model)
  : model(model),
    displaySPI(HSPI),
    leftDisplay(&displaySPI, LEFT_DC, LEFT_CS, LEFT_RST),
    rightDisplay(&displaySPI, RIGHT_DC, RIGHT_CS, RIGHT_RST),
#if TANDO_WOKWI_MODE
    simulatorDisplay(TFT_SCLK, TFT_MOSI, LEFT_CS, RIGHT_CS, LEFT_DC, RIGHT_DC),
#endif
    frame(FRAME_W, FRAME_H) {}

    
#if TANDO_WOKWI_MODE
// static TandoDmaDisplay simulatorDisplay(TFT_SCLK, TFT_MOSI, LEFT_CS, RIGHT_CS, LEFT_DC, RIGHT_DC);
// static TandoTrackedCanvas frame(FRAME_W, FRAME_H);
static_assert(FRAME_W % 2 == 0, "SPI row stride must preserve 32-bit alignment");
static_assert(FRAME_H % 2 == 0, "Background storage uses two equal row blocks");

#else
// static GFXcanvas16 frame(FRAME_W, FRAME_H);
#endif

EyesView eyesView(eyesModel);

#if TANDO_WOKWI_MODE // TANDO_PERF_PROBE


void EyesView::printSimProbe() {
  Serial.printf("SIMPERF uptime_ms=%lu frames=%lu waits=%lu pair_us=%lu draw_us=%lu spi_us=%lu max_loop_ms=%lu bytes=%lu cache_l=%d cache_r=%d bg_cache=%d heap=%lu target_ms=%lu bands=%d\r\n",
                (unsigned long)millis(), (unsigned long)simProbeFrames,
                (unsigned long)simProbeWaits, (unsigned long)simProbePairUs,
                (unsigned long)simProbeDrawUs, (unsigned long)simProbeSpiUs,
                (unsigned long)simProbeMaxGap, (unsigned long)simProbeTransferBytes,
                leftFrameCache != nullptr, rightFrameCache != nullptr,
                renderBackground[0] != nullptr && renderBackground[1] != nullptr,
                (unsigned long)ESP.getFreeHeap(), (unsigned long)simulatorFrameIntervalMs,
                simulatorUseBands);
}
#endif

#if TANDO_WOKWI_MODE

void EyesView::toggleSimulatorFrameInterval() {
  simulatorFrameIntervalMs = simulatorFrameIntervalMs == FRAME_MS ? 120 : FRAME_MS;
  Serial.printf("SIM: target_ms=%lu\r\n", (unsigned long)simulatorFrameIntervalMs);
}

void EyesView::toggleSimulatorTransferMode() {
  simulatorUseBands = !simulatorUseBands;
  Serial.printf("SIM: transfer=%s\r\n", simulatorUseBands ? "BANDS" : "BBOX");
}

void EyesView::noteSimulatorLoop(uint32_t now) {
  if (simProbeLastLoop && now - simProbeLastLoop > simProbeMaxGap) {
    simProbeMaxGap = now - simProbeLastLoop;
  }
  simProbeLastLoop = now;
}

void EyesView::noteSimulatorWait() { simProbeWaits++; }

void EyesView::beginSimulatorFrame() {
  simProbeFrameStart = micros();
  simProbeSpiUs = 0;
  simProbeDrawUs = 0;
  simProbeTransferBytes = 0;
}

void EyesView::endSimulatorFrame() {
  simProbePairUs = micros() - simProbeFrameStart;
  simProbeFrames++;
  uint32_t now = millis();
  if (simProbeFrames == 3 || simProbeFrames == 10 || now - simProbeLastReportMs >= 5000) {
    simProbeLastReportMs = now;
    printSimProbe();
  }
}

void EyesView::setupSimulatorCaches() {
  leftFrameCache = (uint16_t *)malloc(FRAME_W * FRAME_H * sizeof(uint16_t));
  rightFrameCache = (uint16_t *)malloc(FRAME_W * FRAME_H * sizeof(uint16_t));
  Serial.printf("SIM: frame caches L=%s R=%s (OFF uses full frames)\r\n",
                leftFrameCache ? "ON" : "OFF", rightFrameCache ? "ON" : "OFF");
  const size_t backgroundBlockBytes = FRAME_W * (FRAME_H / 2) * sizeof(uint16_t);
  renderBackground[0] = (uint16_t *)malloc(backgroundBlockBytes);
  renderBackground[1] = (uint16_t *)malloc(backgroundBlockBytes);
  if (!renderBackground[0] || !renderBackground[1]) {
    free(renderBackground[0]);
    free(renderBackground[1]);
    renderBackground[0] = renderBackground[1] = nullptr;
  }
  Serial.printf("SIM: render background=%s (OFF uses full clear/scan)\r\n",
                renderBackground[0] && renderBackground[1] ? "ON" : "OFF");
  // Allocate the small DMA staging buffer after the larger frame caches.
  // Display initialization/clear has already completed through Adafruit.
  simulatorDisplay.begin(displaySPI);
}
#endif


uint16_t EyesView::rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) |
         ((g & 0xFC) << 3) |
         (b >> 3);
}

uint16_t EyesView::blend565(uint16_t a, uint16_t b, float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;

  int ar = (a >> 11) & 0x1F;
  int ag = (a >> 5)  & 0x3F;
  int ab = a & 0x1F;

  int br = (b >> 11) & 0x1F;
  int bg = (b >> 5)  & 0x3F;
  int bb = b & 0x1F;

  int r = ar + (int)((br - ar) * t);
  int g = ag + (int)((bg - ag) * t);
  int bl = ab + (int)((bb - ab) * t);

  return (r << 11) | (g << 5) | bl;
}

void EyesView::drawZSymbol(int x, int y, int s, uint16_t color) {
  frame.drawLine(x, y, x + s, y, color);
  frame.drawLine(x + s, y, x, y + s, color);
  frame.drawLine(x, y + s, x + s, y + s, color);
}

void EyesView::drawSleepGraphics(bool leftSide, uint32_t now) {
  if (leftSide || reaction != R_SLEEP || !sleepReaction.sleepTagPresent) return;

  uint32_t elapsed = now - reactionStart;
  if (elapsed < 3200) return;

  // Keep the Z marks gently floating for as long as the tag remains present.
  float t = (float)(elapsed - 3200) / 1000.0f;
  float floatY = fmodf(t * 9.0f, 18.0f);

  uint16_t c = blend565(C_SLEEP_Z, C_WHITE, 0.15f + 0.15f * sinf(now * 0.006f));

  drawZSymbol(142, 58 - (int)floatY, 10, c);
  drawZSymbol(160, 38 - (int)(floatY * 0.7f), 7, c);
}

void EyesView::drawSleepRequestGraphics(bool leftSide, uint32_t now) {
  if (!demo.sleepRequestPending || reaction != R_NONE) return;

  // A subtle pair of floating Z cues supports the heavy-but-open eyelids.
  // Unlike drawSleepGraphics(), this runs on both eyes and never implies the
  // fully closed persistent R_SLEEP state.
  float t = (float)now / 1000.0f + (leftSide ? 0.0f : 0.18f);
  int drift = (int)((sinf(t * 1.35f) + 1.0f) * 2.0f);
  uint16_t c = blend565(C_SLEEP_Z, C_WHITE, 0.06f + 0.10f * (0.5f + 0.5f * sinf(t * 1.35f)));

  drawZSymbol(158, 57 - drift, 6, c);
  drawZSymbol(170, 44 - drift, 4, blend565(c, C_BLACK, 0.20f));
}

#if TANDO_WOKWI_MODE

void EyesView::initProgressRingGeometry() {
  for (uint16_t y = 0; y < FRAME_H; y++) {
    for (uint8_t half = 0; half < 2; half++) {
      progressRingRowSpans[y][half] = {FRAME_W, 0};
    }
  }
  const int segments = 120;      // 3 degrees per segment
  const float segmentDeg = 360.0f / (float)segments;
  const float radius = 96.0f;
  const float centerX = 100.0f;
  const float centerY = 100.0f;

  // Geometry is constant; calculate it once with the target's original math.
  for (int i = 0; i < segments; i++) {
    float startDeg = -90.0f + i * segmentDeg;
    float endDeg = startDeg + segmentDeg;
    float a1 = startDeg * DEG_TO_RAD;
    float a2 = endDeg * DEG_TO_RAD;

    for (int t = 0; t < 4; t++) {
      float r = radius - (float)t;
      ProgressRingSegment &segment = progressRingGeometry[i][t];
      segment.x1 = (int)(centerX + cosf(a1) * r);
      segment.y1 = (int)(centerY + sinf(a1) * r);
      segment.x2 = (int)(centerX + cosf(a2) * r);
      segment.y2 = (int)(centerY + sinf(a2) * r);

      // A line stays within its endpoint bounds; retain both half-row spans.
      int x1 = segment.x1 < segment.x2 ? segment.x1 : segment.x2;
      int x2 = (segment.x1 > segment.x2 ? segment.x1 : segment.x2) + 1;
      int y1 = segment.y1 < segment.y2 ? segment.y1 : segment.y2;
      int y2 = segment.y1 > segment.y2 ? segment.y1 : segment.y2;
      if (y2 >= FRAME_H) y2 = FRAME_H - 1;
      for (int y = y1; y <= y2; y++) {
        for (uint8_t half = 0; half < 2; half++) {
          int halfFirst = half ? FRAME_W / 2 : 0;
          int halfEnd = half ? FRAME_W : FRAME_W / 2;
          int first = x1 > halfFirst ? x1 : halfFirst;
          int end = x2 < halfEnd ? x2 : halfEnd;
          if (first < end) {
            ProgressRingRowSpan &span = progressRingRowSpans[y][half];
            if (first < span.first) span.first = first;
            if (end > span.end) span.end = end;
          }
        }
      }
    }
  }
}

#endif

uint16_t EyesView::getRingColor() {
  // During an unlock, show the color of the unlock being presented even if
  // currentStage has already advanced further while another state was active.
  if (reaction == R_UNLOCK2) return C_RING_S2;
  if (reaction == R_UNLOCK3 || reaction == R_COMPLETE || demo.completed) return C_RING_S3;
  if (demo.stage == 1) return C_RING_S1;
  if (demo.stage == 2) return C_RING_S2;
  return C_RING_S3;
}

void EyesView::drawProgressRing(uint32_t now) {
  const int segments = 120;      // 3 degrees per segment
  const float segmentDeg = 360.0f / (float)segments;
#if !TANDO_WOKWI_MODE
  const float radius = 96.0f;
  const float centerX = 100.0f;
  const float centerY = 100.0f;
#endif

  float progress = (float)demo.visualCredits / (float)TOTAL_VISUAL_CREDITS;
  progress = EyesModel::clamp01(progress);

  // Symmetric fill: start at 12 o'clock and grow equally to the left and right.
  // At 50%, 90 degrees are filled on each side. At 100%, the ring is complete.
  float halfSpanDeg = progress * 180.0f;

  float pulse = model.getRingPulseAmount(now);
  uint16_t ringColor = blend565(getRingColor(), C_WHITE, pulse * 0.55f);

  int thickness = 3;
  if (pulse > 0.35f) thickness = 4;

  for (int i = 0; i < segments; i++) {
    float startDeg = -90.0f + i * segmentDeg;
#if !TANDO_WOKWI_MODE
    float endDeg = startDeg + segmentDeg;
#endif
    float midDeg = startDeg + segmentDeg * 0.5f;

#if TANDO_WOKWI_MODE
    const ProgressRingSegment &track = progressRingGeometry[i][0];
    frame.drawLine(track.x1, track.y1, track.x2, track.y2, C_RING_TRACK);
#else
    float a1 = startDeg * DEG_TO_RAD;
    float a2 = endDeg * DEG_TO_RAD;

    // Thin dark track.
    int tx1 = (int)(centerX + cosf(a1) * radius);
    int ty1 = (int)(centerY + sinf(a1) * radius);
    int tx2 = (int)(centerX + cosf(a2) * radius);
    int ty2 = (int)(centerY + sinf(a2) * radius);
    frame.drawLine(tx1, ty1, tx2, ty2, C_RING_TRACK);
#endif

    // Angular distance from the 12 o'clock origin, normalized to -180..+180.
    float delta = midDeg - (-90.0f);
    while (delta > 180.0f) delta -= 360.0f;
    while (delta < -180.0f) delta += 360.0f;

    if (fabsf(delta) <= halfSpanDeg) {
      for (int t = 0; t < thickness; t++) {
#if TANDO_WOKWI_MODE
        const ProgressRingSegment &segment = progressRingGeometry[i][t];
        frame.drawLine(segment.x1, segment.y1, segment.x2, segment.y2, ringColor);
#else
        float r = radius - (float)t;
        int x1 = (int)(centerX + cosf(a1) * r);
        int y1 = (int)(centerY + sinf(a1) * r);
        int x2 = (int)(centerX + cosf(a2) * r);
        int y2 = (int)(centerY + sinf(a2) * r);
        frame.drawLine(x1, y1, x2, y2, ringColor);
#endif
      }
    }
  }
}

void EyesView::drawCompletionSparkles(bool leftSide, uint32_t now) {
  if (reaction != R_COMPLETE) return;

  uint32_t elapsed = now - reactionStart;
  float phase = elapsed * 0.008f;

  for (int i = 0; i < 6; i++) {
    float a = phase + i * (TWO_PI / 6.0f) + (leftSide ? 0.0f : 0.35f);
    float rr = 78.0f + sinf(phase * 0.7f + i) * 7.0f;
    int x = (int)(SCREEN_CX + cosf(a) * rr);
    int y = (int)(SCREEN_CY + sinf(a) * rr);
    int r = 1 + ((i + (elapsed / 180)) % 2);
    frame.fillCircle(x, y, r, C_SPARK);
  }
}

void EyesView::drawChickenDrumstick(int cx, int cy, float phase) {
  uint16_t meatDark = rgb565(145, 63, 28);
  uint16_t meat = rgb565(205, 93, 39);
  uint16_t meatHi = rgb565(238, 132, 58);
  uint16_t bone = rgb565(240, 224, 190);
  uint16_t boneHi = rgb565(255, 247, 224);

  int wiggle = (int)(sinf(phase * 1.7f) * 2.0f);
  int bob = (int)(sinf(phase * 2.2f) * 2.0f);
  cx += wiggle;
  cy += bob;

  // Meat head, angled slightly toward the center.
  frame.fillCircle(cx - 8, cy - 4, 12, meatDark);
  frame.fillCircle(cx + 2, cy - 8, 13, meatDark);
  frame.fillCircle(cx - 7, cy - 5, 10, meat);
  frame.fillCircle(cx + 2, cy - 8, 11, meat);
  frame.fillTriangle(cx - 13, cy + 1, cx + 10, cy - 16, cx + 12, cy + 7, meat);
  frame.fillCircle(cx - 4, cy - 10, 4, meatHi);

  // Bone shaft. Multiple lines make it readable at 200x200.
  for (int o = -2; o <= 2; o++) {
    frame.drawLine(cx + 7, cy + 5 + o, cx + 25, cy + 15 + o, bone);
  }
  frame.drawLine(cx + 8, cy + 4, cx + 24, cy + 13, boneHi);

  // Bone knobs.
  frame.fillCircle(cx + 28, cy + 15, 5, bone);
  frame.fillCircle(cx + 25, cy + 19, 5, bone);
  frame.fillCircle(cx + 27, cy + 14, 2, boneHi);
}

void EyesView::drawHungerFoodOverlay(bool leftSide, uint32_t now) {
  if (!hungerRequest.promptActive || reaction != R_NONE || demo.sleepRequestPending) return;

  uint32_t elapsed = now - hungerRequest.promptStart;
  float phase = (float)elapsed / 1000.0f;

  // Keep the cue low enough not to replace the eye, but high enough to stay
  // inside the 200x200 canvas and inside the progress ring.
  // Same Hunger cue on both displays, as requested.
  // A tiny phase offset prevents the pair from looking mechanically mirrored.
  float displayPhase = phase + (leftSide ? 0.0f : 0.18f);
  drawChickenDrumstick(100, 164, displayPhase);

  // Small attention pulse under the icon, still below the eye.
  float pulse = 0.5f + 0.5f * sinf(phase * 4.5f);
  uint16_t cue = blend565(getRingColor(), C_WHITE, 0.25f + pulse * 0.30f);
  int r = 2 + (int)(pulse * 2.0f);
  frame.fillCircle(100, 188, r, cue);
}

void EyesView::drawPetRequestHeart(int cx, int cy, int size, uint16_t color) {
  int r = size / 2;
  if (r < 3) r = 3;

  frame.fillCircle(cx - r, cy - r / 2, r, color);
  frame.fillCircle(cx + r, cy - r / 2, r, color);
  frame.fillTriangle(
    cx - size,
    cy - r / 2,
    cx + size,
    cy - r / 2,
    cx,
    cy + size + 3,
    color
  );
}

void EyesView::drawPetRequestParen(int cx, int cy, int span, bool leftSide, uint16_t color) {
  int dir = leftSide ? -1 : 1;
  int x0 = cx + dir * span;
  int x1 = x0 + dir * 4;
  int x2 = x0 + dir * 7;

  // Four line segments form a clean parenthesis-like broadcast wave.
  frame.drawLine(x0, cy - 14, x1, cy - 10, color);
  frame.drawLine(x1, cy - 10, x2, cy, color);
  frame.drawLine(x2, cy, x1, cy + 10, color);
  frame.drawLine(x1, cy + 10, x0, cy + 14, color);
}

void EyesView::drawPetRequestOverlay(bool leftSide, uint32_t now) {
  if (!petRequest.promptActive || reaction != R_NONE || demo.sleepRequestPending) return;

  uint32_t elapsed = now - petRequest.promptStart;
  float phase = (float)elapsed / 1000.0f + (leftSide ? 0.0f : 0.15f);

  // Heart pulse: affectionate rather than alarmingly fast.
  float heartPulse = 0.5f + 0.5f * sinf(phase * 4.2f);
  int heartSize = 9 + (int)(heartPulse * 2.5f);
  uint16_t heartColor = blend565(C_BLUSH, C_WHITE, 0.12f + heartPulse * 0.30f);

  // Two parenthesis waves on each side alternate brightness, reading as
  // (( heart )) / attention broadcast without using text or a hand icon.
  float innerWave = 0.5f + 0.5f * sinf(phase * 4.2f);
  float outerWave = 0.5f + 0.5f * sinf(phase * 4.2f - 1.25f);
  uint16_t innerColor = blend565(C_BLUSH, C_WHITE, 0.10f + innerWave * 0.55f);
  uint16_t outerColor = blend565(C_BLUSH, C_WHITE, 0.05f + outerWave * 0.45f);

  const int cx = 100;
  const int cy = 164;

  drawPetRequestHeart(cx, cy, heartSize, heartColor);

  drawPetRequestParen(cx, cy, 21, true, innerColor);
  drawPetRequestParen(cx, cy, 21, false, innerColor);
  drawPetRequestParen(cx, cy, 33, true, outerColor);
  drawPetRequestParen(cx, cy, 33, false, outerColor);

  // Tiny center flash reinforces the request rhythm while remaining soft.
  if (heartPulse > 0.72f) {
    frame.fillCircle(cx, cy - 4, 2, C_WHITE);
  }
}

void EyesView::drawFantasyEye(bool leftSide, uint32_t now) {
  const EyeMotion &eye = leftSide ? model.leftEye : model.rightEye;

  // Tiny living micro drift.
  float microX = sinf(now * 0.0017f + (leftSide ? 0.0f : 0.35f)) * 0.45f;
  float microY = sinf(now * 0.00115f + (leftSide ? 0.2f : 0.5f)) * 0.35f;

  float cx = SCREEN_CX + eye.x + microX;
  float cy = EYE_BASE_Y + eye.y + microY;

  float w = 100.0f + model.surpriseBlend * 8.0f;
  float h = 58.0f + model.surpriseBlend * 14.0f;

  // Unlock/completion makes eye breathe slightly larger.
  if (reaction == R_UNLOCK2 || reaction == R_UNLOCK3 || reaction == R_COMPLETE) {
    float pulse = 0.5f + 0.5f * sinf(now * 0.010f);
    w += 4.0f * pulse;
    h += 3.0f * pulse;
  }

  float effectiveBlink = model.blinkAmount;
  float personalityLid = model.autonomousLidAmount(leftSide, now);
  if (personalityLid > effectiveBlink) effectiveBlink = personalityLid;

  float requestLid = model.petRequestLidAmount(now);
  if (requestLid > effectiveBlink) effectiveBlink = requestLid;

  float sleepRequestLid = model.sleepRequestLidAmount(now);
  if (sleepRequestLid > effectiveBlink) effectiveBlink = sleepRequestLid;

  float closeAmount = effectiveBlink;

  if (model.sleepClose > effectiveBlink && model.sleepClose > 0.001f) {
    // Sleep closure is top-lid dominant. Keep the lower edge almost fixed
    // while the upper lid settles downward. This reads more naturally than
    // shrinking the eye equally from top and bottom.
    float originalH = h;
    h *= (1.0f - model.sleepClose * 0.93f);
    if (h < 4.0f) h = 4.0f;
    cy += (originalH - h) * 0.46f;
    closeAmount = model.sleepClose;
  } else {
    // Normal blink is also top-lid dominant. Keep the lower edge nearly fixed
    // instead of collapsing the eye equally from the top and bottom.
    float originalH = h;
    h *= (1.0f - effectiveBlink * 0.93f);
    if (h < 4.0f) h = 4.0f;
    cy += (originalH - h) * 0.46f;
  }

  // PET has its own slow, shallow eyelid soften. It is intentionally much
  // gentler than a normal blink: the eyes stay open, but briefly "melt" into
  // the affectionate pose, which makes the reaction more noticeable.
  if (reaction == R_PET && model.sleepClose < 0.01f && effectiveBlink < 0.01f) {
    uint32_t petElapsed = now - reactionStart;
    float petSoftClose = 0.0f;

    if (petElapsed >= 1550 && petElapsed < 2350) {
      float p = (float)(petElapsed - 1550) / 800.0f;
      petSoftClose = sinf(EyesModel::clamp01(p) * PI) * 0.13f;
    } else if (petElapsed >= 2900 && petElapsed < 3500) {
      float p = (float)(petElapsed - 2900) / 600.0f;
      petSoftClose = sinf(EyesModel::clamp01(p) * PI) * 0.07f;
    }

    if (petSoftClose > 0.001f) {
      float originalH = h;
      h *= (1.0f - petSoftClose);
      cy += (originalH - h) * 0.20f;
    }
  }

  int iw = (int)w;
  int ih = (int)h;
  int ix = (int)(cx - w / 2.0f);
  int iy = (int)(cy - h / 2.0f);

  int radius = (int)(h / 2.0f);
  if (radius > 22) radius = 22;
  if (radius < 2) radius = 2;

  uint16_t glowColor = blend565(C_EYE_GLOW, C_EYE, model.specialGlow * 0.50f);
  int glowPad = 3 + (int)(model.specialGlow * 3.0f);

  frame.fillRoundRect(
    ix - glowPad,
    iy - glowPad,
    iw + glowPad * 2,
    ih + glowPad * 2,
    radius + glowPad,
    glowColor
  );

  frame.fillRoundRect(ix, iy, iw, ih, radius, C_EYE);

  if (closeAmount < 0.40f) {
    frame.fillRoundRect(ix + 17, iy + 10, 20, 4, 2, C_EYE_SHINE);
  }

  // Happy / affectionate eye: raise lower eyelid.
  if (model.happyBlend > 0.02f) {
    int cover = (int)(h * 0.39f * model.happyBlend);
    frame.fillRect(ix - 2, iy + ih - cover, iw + 4, cover + 4, C_BLACK);

    if (model.happyBlend > 0.50f) {
      int bx = leftSide ? 35 : 165;
      frame.fillCircle(bx, 132, 4, C_BLUSH);
    }
  }

  // PET adds a visible cheek blush and a small inner sparkle.
  // These accents make the affectionate state clearly different from IDLE.
  if (reaction == R_PET && closeAmount < 0.45f) {
    uint32_t petElapsed = now - reactionStart;
    float petIn = smoothStep(EyesModel::clamp01((float)petElapsed / 700.0f));
    float petOut = 1.0f;
    if (petElapsed > 3850) {
      petOut = 1.0f - smoothStep(EyesModel::clamp01((float)(petElapsed - 3850) / 950.0f));
    }

    float petStrength = petIn * petOut;
    if (petStrength > 0.15f) {
      int bx = leftSide ? 34 : 166;
      int blushR = (petStrength > 0.70f) ? 5 : 4;
      frame.fillCircle(bx, 132, blushR, C_BLUSH);
      frame.fillCircle(bx + (leftSide ? 8 : -8), 135, 2, C_BLUSH);

      // Sparkle is on the inner/top side of each eye, reinforcing the
      // "looking up at you" expression.
      int sx = leftSide ? (ix + iw - 24) : (ix + 24);
      int sy = iy + 12;
      int sr = (petStrength > 0.65f) ? 4 : 3;
      frame.fillCircle(sx, sy, sr, C_WHITE);
      frame.fillCircle(sx + (leftSide ? -6 : 6), sy + 7, 1, C_WHITE);
    }
  }

}

#if TANDO_WOKWI_MODE
uint16_t *EyesView::renderBackgroundRow(uint16_t y) {
  const uint16_t halfHeight = FRAME_H / 2;
  return renderBackground[y / halfHeight] + (y % halfHeight) * FRAME_W;
}
void EyesView::restoreRenderBackground(TandoFrameRegion region) {
  for (uint16_t y = region.y; y < region.y + region.height; y++) {
    memcpy(frame.getBuffer() + y * FRAME_W + region.x,
           renderBackgroundRow(y) + region.x, region.width * sizeof(uint16_t));
  }
}
TandoRingBackgroundKey EyesView::getRenderBackgroundKey(uint32_t now) {
  TandoRingBackgroundKey key = {};
  key.visible = reaction == R_NONE;
  if (key.visible) {
    float pulse = model.getRingPulseAmount(now);
    key.credits = demo.visualCredits;
    key.thickness = pulse > 0.35f ? 4 : 3;
    key.color = blend565(getRingColor(), C_WHITE, pulse * 0.55f);
  }
  return key;
}
void EyesView::pushFrame(bool leftSide) {
  uint16_t *buffer = frame.getBuffer();
  Adafruit_GC9A01A &display = leftSide ? leftDisplay : rightDisplay;
  uint32_t simProbeStart = micros();
  uint16_t *previous = leftSide ? leftFrameCache : rightFrameCache;
  bool &valid = leftSide ? leftFrameCacheValid : rightFrameCacheValid;
  TandoFramePlan plan = {};
  if (simulatorUseBands) {
    plan = tando_changed_bands_in(buffer, valid ? previous : nullptr,
                                 FRAME_W, FRAME_H, renderDiffRegion);
  } else {
    TandoFrameRegion region = tando_changed_region_in(buffer, valid ? previous : nullptr,
                                                     FRAME_W, FRAME_H, renderDiffRegion);
    if (region.width && region.height) { plan.regions[0] = region; plan.count = 1; }
  }
  if (!plan.count) {
    simProbeSpiUs += micros() - simProbeStart;
    return;
  }

  // Both displays render sequentially, so they can reuse this fixed buffer.
  alignas(4) static uint16_t packedRows[800];
  const bool dma = simulatorDisplay.active();
  if (!dma) display.startWrite();
  for (uint8_t i = 0; i < plan.count; i++) {
    TandoFrameRegion region = plan.regions[i];
    if (dma) {
      if (!simulatorDisplay.writeRegion(leftSide, buffer + region.y * FRAME_W + region.x,
                                       FRAME_W, FRAME_X + region.x, FRAME_Y + region.y,
                                       region.width, region.height)) {
        // A partial transfer must never be committed as a complete cached image.
        valid = false;
        simProbeSpiUs += micros() - simProbeStart;
        return;
      }
    } else {
      display.setAddrWindow(FRAME_X + region.x, FRAME_Y + region.y, region.width, region.height);
      if (region.width == FRAME_W) {
        display.writePixels(buffer + region.y * FRAME_W, region.width * region.height);
      } else {
        uint16_t rowsPerChunk = sizeof(packedRows) / (region.width * sizeof(uint16_t));
        for (uint16_t y = region.y; y < region.y + region.height; ) {
          uint16_t rows = region.y + region.height - y;
          if (rows > rowsPerChunk) rows = rowsPerChunk;
          for (uint16_t row = 0; row < rows; row++) {
            memcpy(packedRows + row * region.width, buffer + (y + row) * FRAME_W + region.x,
                   region.width * sizeof(uint16_t));
          }
          display.writePixels(packedRows, rows * region.width);
          y += rows;
        }
      }
    }
    simProbeTransferBytes += region.width * region.height * sizeof(uint16_t);
  }
  if (!dma) display.endWrite();
  if (previous) {
    for (uint8_t i = 0; i < plan.count; i++) {
      TandoFrameRegion region = plan.regions[i];
      if (region.width == FRAME_W) {
        memcpy(previous + region.y * FRAME_W, buffer + region.y * FRAME_W,
               region.width * region.height * sizeof(uint16_t));
      } else {
        for (uint16_t y = region.y; y < region.y + region.height; y++) {
          memcpy(previous + y * FRAME_W + region.x, buffer + y * FRAME_W + region.x,
                 region.width * sizeof(uint16_t));
        }
      }
    }
    valid = true;
  }
  simProbeSpiUs += micros() - simProbeStart;
}
#endif

void EyesView::pushLeft() {
  uint16_t *buffer = frame.getBuffer();

  leftDisplay.startWrite();
  leftDisplay.setAddrWindow(FRAME_X, FRAME_Y, FRAME_W, FRAME_H);
  leftDisplay.writePixels(buffer, FRAME_W * FRAME_H);
  leftDisplay.endWrite();
}

void EyesView::pushRight() {
  uint16_t *buffer = frame.getBuffer();

  rightDisplay.startWrite();
  rightDisplay.setAddrWindow(FRAME_X, FRAME_Y, FRAME_W, FRAME_H);
  rightDisplay.writePixels(buffer, FRAME_W * FRAME_H);
  rightDisplay.endWrite();
}

void EyesView::renderDisplay(bool leftSide, uint32_t now) {
#if TANDO_WOKWI_MODE // TANDO_PERF_PROBE
  uint32_t simProbeStart = micros();
#endif // TANDO_PERF_PROBE
#if TANDO_WOKWI_MODE
  TandoRingBackgroundKey backgroundKey = getRenderBackgroundKey(now);
  // The fixed ring palette is opaque and non-black; other palettes use full rendering.
  bool useBackground = renderBackground[0] && renderBackground[1] &&
                       frame.getRotation() == 0 && C_BLACK == 0 && C_RING_TRACK != 0 &&
                       (!backgroundKey.visible || backgroundKey.color != C_BLACK);
  renderDiffRegion = {0, 0, FRAME_W, FRAME_H};
  if (useBackground) {
    if (!renderBackgroundValid || !(backgroundKey == renderBackgroundKey)) {
      frame.fillScreen(C_BLACK);
      if (backgroundKey.visible) drawProgressRing(now);
      for (uint16_t y = 0; y < FRAME_H; y++) {
        memcpy(renderBackgroundRow(y), frame.getBuffer() + y * FRAME_W,
               FRAME_W * sizeof(uint16_t));
      }
      renderBackgroundKey = backgroundKey;
      renderBackgroundValid = true;
    } else {
      restoreRenderBackground(lastCanvasDynamic);
    }
    frame.beginTracking();
  } else {
    renderBackgroundValid = false;
    frame.fillScreen(C_BLACK);
  }
#else
  frame.fillScreen(C_BLACK);
#endif

  // Hunger no longer replaces the face. The normal eye keeps its full state
  // machine (idle, Blink, Wink, Smile, Play and user reactions).
  drawFantasyEye(leftSide, now);
  drawSleepGraphics(leftSide, now);
  drawSleepRequestGraphics(leftSide, now);
  drawCompletionSparkles(leftSide, now);

  if (reaction == R_NONE && !demo.sleepRequestPending && hungerRequest.promptActive) {
    drawHungerFoodOverlay(leftSide, now);
  }

  if (reaction == R_NONE && !demo.sleepRequestPending && petRequest.promptActive) {
    drawPetRequestOverlay(leftSide, now);
  }

  // During an actual reaction, show only that reaction's eye/graphics.
  // The persistent Progress Ring returns immediately after the reaction ends.
#if TANDO_WOKWI_MODE
  TandoEyeRenderState &eyeState = eyeRenderState[leftSide ? 0 : 1];
  if (useBackground) {
    TandoFrameRegion dynamic = frame.endTracking();
    // Preserve the original ring-LAST occlusion where dynamic graphics overlap it.
    if (backgroundKey.visible) {
      for (uint16_t y = dynamic.y; y < dynamic.y + dynamic.height; y++) {
        const uint16_t *background = renderBackgroundRow(y);
        uint16_t *pixels = frame.getBuffer() + y * FRAME_W;
        for (uint8_t half = 0; half < 2; half++) {
          const ProgressRingRowSpan &span = progressRingRowSpans[y][half];
          uint16_t first = dynamic.x > span.first ? dynamic.x : span.first;
          uint16_t right = dynamic.x + dynamic.width;
          uint16_t end = right < span.end ? right : span.end;
          for (uint16_t x = first; x < end; x++) {
            if (background[x] != C_BLACK) pixels[x] = background[x];
          }
        }
      }
    }
    if (eyeState.keyValid && backgroundKey == eyeState.key) {
      renderDiffRegion = tando_union_region(eyeState.dynamic, dynamic);
    }
    eyeState.key = backgroundKey;
    eyeState.keyValid = true;
    eyeState.dynamic = dynamic;
    lastCanvasDynamic = dynamic;
  } else {
    eyeState.keyValid = false;
    if (reaction == R_NONE) drawProgressRing(now);
  }
#else
  if (reaction == R_NONE) {
    drawProgressRing(now);
  }
#endif

#if TANDO_WOKWI_MODE // TANDO_PERF_PROBE
  simProbeDrawUs += micros() - simProbeStart;
#endif // TANDO_PERF_PROBE
#if TANDO_WOKWI_MODE
  pushFrame(leftSide);
#else
  if (leftSide) {
    pushLeft();
  } else {
    pushRight();
  }
#endif
}

#if TANDO_WOKWI_MODE
void EyesView::printSimulatorConfiguration() {
  Serial.printf("SIM CONFIG: cpu_mhz=%u apb_hz=%lu spi_div=0x%lX psram_bytes=%lu canvas=%p cache_l=%p cache_r=%p transport=%s configured_spi_hz=40000000\r\n",
                (unsigned)getCpuFrequencyMhz(), (unsigned long)getApbFrequency(),
                (unsigned long)(simulatorDisplay.active() ? 0 : displaySPI.getClockDivider()),
                (unsigned long)ESP.getPsramSize(),
                (void *)frame.getBuffer(), (void *)leftFrameCache, (void *)rightFrameCache,
                simulatorDisplay.active() ? "DMA" : "FIFO");
}
#endif

void EyesView::setupEyes() {
  // ----------------------------------------------------------
  // Canvas
  // ----------------------------------------------------------
  if (frame.getBuffer() == nullptr) {
    Serial.println("FATAL: CANVAS ALLOCATION FAILED");
    while (true) delay(1000);
  }

#if TANDO_WOKWI_MODE
  initProgressRingGeometry();
#endif

  // ----------------------------------------------------------
  // Display SPI
  // ----------------------------------------------------------
  pinMode(LEFT_CS, OUTPUT);
  pinMode(RIGHT_CS, OUTPUT);
  // این دو تا وقتی که پایین باشند فعال هستند پس میبریمشون روی بالا
  digitalWrite(LEFT_CS, HIGH);
  digitalWrite(RIGHT_CS, HIGH);

  displaySPI.begin(TFT_SCLK, -1, TFT_MOSI, -1);

  leftDisplay.begin(40000000);
  rightDisplay.begin(40000000);
  leftDisplay.setRotation(0);
  rightDisplay.setRotation(0);

  // ----------------------------------------------------------
  // Colors
  // ----------------------------------------------------------
  C_BLACK      = rgb565(0, 0, 0);
  C_WHITE      = rgb565(255, 255, 255);

  C_EYE        = rgb565(65, 235, 255);
  C_EYE_GLOW   = rgb565(0, 58, 76);
  C_EYE_SHINE  = rgb565(225, 255, 255);

  C_BLUSH      = rgb565(255, 80, 145);

  C_RING_TRACK = rgb565(7, 22, 27);
  C_RING_S1    = rgb565(85, 225, 150);   // soft green
  C_RING_S2    = rgb565(55, 220, 255);   // cyan / blue
  C_RING_S3    = rgb565(255, 195, 65);   // warm gold

  C_SLEEP_Z    = rgb565(130, 190, 255);
  C_SPARK      = rgb565(255, 235, 145);

  leftDisplay.fillScreen(C_BLACK);
  rightDisplay.fillScreen(C_BLACK);

  Serial.println("EYES READY");
}