#include "Arduino.h"
#include "firmware/tando_final_demo_15min/Eyes.h"
#include <cstdarg>
#include <cstdio>

// Platform state only. All toy behavior and rendering stay in canonical modules.
uint16_t previewPixels[2][240 * 240];
PreviewSerial Serial;
PreviewEsp ESP;

static uint32_t clockMs = 0;
static uint32_t baseMs = 0;
static uint32_t bootSeed = 0x54414e44;
static uint32_t randomState = 0x54414e44;
static uint32_t ledDuty = 0;
static unsigned char inputs[256];
static unsigned inputHead = 0;
static unsigned inputTail = 0;

extern uint32_t simProbeFrames;
extern "C" __attribute__((import_module("env"), import_name("preview_now_us")))
double previewNowUs();

uint32_t millis() { return clockMs; }
uint32_t micros() { return uint32_t(uint64_t(previewNowUs())); }
void delay(uint32_t amount) { clockMs += amount; }

void randomSeed(unsigned long seed) {
  randomState = seed ? uint32_t(seed) : 1;
}

uint32_t esp_random() { return bootSeed; }

static uint32_t nextRandom() {
  randomState ^= randomState << 13;
  randomState ^= randomState >> 17;
  randomState ^= randomState << 5;
  return randomState;
}

long random(long maximum) {
  return maximum > 0 ? long(nextRandom() % uint32_t(maximum)) : 0;
}

long random(long minimum, long maximum) {
  return maximum > minimum ? minimum + random(maximum - minimum) : minimum;
}

void pinMode(int, int) {}
void digitalWrite(int, int) {}
double ledcSetup(int, double hz, int) { return hz; }
void ledcAttachPin(int, int) {}
void ledcWrite(int, uint32_t duty) { ledDuty = duty; }
uint32_t getCpuFrequencyMhz() { return 0; }
uint32_t getApbFrequency() { return 0; }

int PreviewSerial::available() const {
  return (inputTail + 256 - inputHead) % 256;
}

int PreviewSerial::read() {
  if (inputHead == inputTail) return -1;
  unsigned char value = inputs[inputHead];
  inputHead = (inputHead + 1) % 256;
  return value;
}

void PreviewSerial::push(int value) {
  unsigned next = (inputTail + 1) % 256;
  if (next != inputHead) {
    inputs[inputTail] = value;
    inputTail = next;
  }
}

size_t PreviewSerial::write(uint8_t value) {
  return fwrite(&value, 1, 1, stdout);
}

size_t PreviewSerial::write(const uint8_t *data, size_t count) {
  return fwrite(data, 1, count, stdout);
}

int PreviewSerial::printf(const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  int length = vprintf(format, arguments);
  va_end(arguments);
  return length;
}

void setup();
void loop();

extern "C" {

__attribute__((export_name("tando_seed"))) void tando_seed(uint32_t seed) {
  bootSeed = seed ? seed : 1;
}

__attribute__((export_name("tando_setup"))) void tando_setup() {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  setup();
  simulatorFrameIntervalMs = 16;
  baseMs = clockMs;
  puts("WEB PREVIEW: shared C++ behavior; no ESP/SPI/electrical simulation");
}

__attribute__((export_name("tando_step"))) void tando_step(uint32_t elapsedMs) {
  clockMs = baseMs + elapsedMs;
  loop();
}

__attribute__((export_name("tando_input"))) void tando_input(int ascii) {
  Serial.push(ascii);
}

__attribute__((export_name("tando_left"))) uint16_t *tando_left() {
  return previewPixels[0];
}

__attribute__((export_name("tando_right"))) uint16_t *tando_right() {
  return previewPixels[1];
}

__attribute__((export_name("tando_frames"))) uint32_t tando_frames() {
  return simProbeFrames;
}

__attribute__((export_name("tando_time"))) uint32_t tando_time() {
  return clockMs;
}

__attribute__((export_name("tando_led"))) uint32_t tando_led() {
  return ledDuty;
}

}
