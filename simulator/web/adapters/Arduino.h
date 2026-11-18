#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#define PROGMEM
#define PI 3.1415926535897932384626433832795
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define radians(a) ((a) * DEG_TO_RAD)
using byte = uint8_t;
constexpr int HIGH = 1, LOW = 0, OUTPUT = 1, HEX = 16;
class __FlashStringHelper;
class String : public std::string {
 public:
  using std::string::string;
};
uint32_t millis();
uint32_t micros();
void delay(uint32_t);
void randomSeed(unsigned long);
long random(long);
long random(long, long);
void pinMode(int, int);
void digitalWrite(int, int);
double ledcSetup(int, double, int);
void ledcAttachPin(int, int);
void ledcWrite(int, uint32_t);
uint32_t getCpuFrequencyMhz();
uint32_t getApbFrequency();
struct PreviewEsp {
  uint32_t getFreeHeap() const { return 0; }
  uint32_t getPsramSize() const { return 0; }
};
extern PreviewEsp ESP;

#include "Serial.h"
