#ifndef TANDO_EYES_VIEW_H
#define TANDO_EYES_VIEW_H

#include <Arduino.h>
#include "../TandoConfig.h"
#include "../model/EyesModel.h"
#if TANDO_WOKWI_MODE
#include "../../../../simulator/wokwi/tracked_canvas.h"
#include "../../../../simulator/wokwi/dma_display.h"
#endif
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>

#if TANDO_WOKWI_MODE
struct ProgressRingSegment {
  uint8_t x1;
  uint8_t y1;
  uint8_t x2;
  uint8_t y2;
};

// Conservative support for every cached ring segment and all four thicknesses.
// Each half-row stores [first, end), including any Bresenham gaps inside it.
struct ProgressRingRowSpan {
  uint8_t first;
  uint8_t end;
};
#endif

// View half of the old Eyes module.
// Owns the displays, the shared canvas and all drawing. It reads the eye
// state from EyesModel and never changes it.
class EyesView {
public:
  explicit EyesView(const EyesModel &model);

  void setupEyes();
  void renderDisplay(bool leftSide, uint32_t now);

#if TANDO_WOKWI_MODE
  // Optional diagnostics for virtual-time simulation; absent from hardware builds.
  uint32_t simulatorFrameIntervalMs = FRAME_MS;
  void toggleSimulatorFrameInterval();
  void toggleSimulatorTransferMode();
  void printSimProbe();
  void printSimulatorConfiguration();
  void setupSimulatorCaches();
  void noteSimulatorLoop(uint32_t now);
  void noteSimulatorWait();
  void beginSimulatorFrame();
  void endSimulatorFrame();
#endif

private:
  const EyesModel &model;

  // Declaration order = construction order (see the EyesView constructor).
  SPIClass displaySPI;
  Adafruit_GC9A01A leftDisplay;
  Adafruit_GC9A01A rightDisplay;
#if TANDO_WOKWI_MODE
  TandoDmaDisplay simulatorDisplay;
  TandoTrackedCanvas frame;
  uint16_t *leftFrameCache = nullptr;
  uint16_t *rightFrameCache = nullptr;
  bool leftFrameCacheValid = false;
  bool rightFrameCacheValid = false;
  uint16_t *renderBackground[2] = {nullptr, nullptr};
  bool renderBackgroundValid = false;
  TandoRingBackgroundKey renderBackgroundKey = {};
  TandoFrameRegion lastCanvasDynamic = {0, 0, 0, 0};
  TandoFrameRegion renderDiffRegion = {0, 0, FRAME_W, FRAME_H};
  TandoEyeRenderState eyeRenderState[2];
  bool simulatorUseBands = true;
#else
  GFXcanvas16 frame;
#endif

#if TANDO_WOKWI_MODE // TANDO_PERF_PROBE
  uint32_t simProbePairUs = 0;
  uint32_t simProbeDrawUs = 0;
  uint32_t simProbeSpiUs = 0;
  uint32_t simProbeFrames = 0;
  uint32_t simProbeWaits = 0;
  uint32_t simProbeLastLoop = 0;
  uint32_t simProbeMaxGap = 0;
  uint32_t simProbeTransferBytes = 0;
  uint32_t simProbeLastReportMs = 0;
#endif // TANDO_PERF_PROBE
#if TANDO_WOKWI_MODE
  uint32_t simProbeFrameStart = 0;
  ProgressRingSegment progressRingGeometry[120][4];
  ProgressRingRowSpan progressRingRowSpans[FRAME_H][2];
#endif

  uint16_t C_BLACK = 0;
  uint16_t C_WHITE = 0;
  uint16_t C_EYE = 0;
  uint16_t C_EYE_GLOW = 0;
  uint16_t C_EYE_SHINE = 0;
  uint16_t C_BLUSH = 0;
  uint16_t C_RING_TRACK = 0;
  uint16_t C_RING_S1 = 0;
  uint16_t C_RING_S2 = 0;
  uint16_t C_RING_S3 = 0;
  uint16_t C_SLEEP_Z = 0;
  uint16_t C_SPARK = 0;

  static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);
  static uint16_t blend565(uint16_t a, uint16_t b, float t);

  void drawZSymbol(int x, int y, int s, uint16_t color);
  void drawSleepGraphics(bool leftSide, uint32_t now);
  void drawSleepRequestGraphics(bool leftSide, uint32_t now);
  uint16_t getRingColor();
  void drawProgressRing(uint32_t now);
  void drawCompletionSparkles(bool leftSide, uint32_t now);
  void drawChickenDrumstick(int cx, int cy, float phase);
  void drawHungerFoodOverlay(bool leftSide, uint32_t now);
  void drawPetRequestHeart(int cx, int cy, int size, uint16_t color);
  void drawPetRequestParen(int cx, int cy, int span, bool leftSide, uint16_t color);
  void drawPetRequestOverlay(bool leftSide, uint32_t now);
  void drawFantasyEye(bool leftSide, uint32_t now);
  void pushLeft();
  void pushRight();
#if TANDO_WOKWI_MODE
  void initProgressRingGeometry();
  uint16_t *renderBackgroundRow(uint16_t y);
  void restoreRenderBackground(TandoFrameRegion region);
  TandoRingBackgroundKey getRenderBackgroundKey(uint32_t now);
  void pushFrame(bool leftSide);
#endif
};

extern EyesView eyesView;

#endif 