#ifndef TANDO_DEMO_STORAGE_H
#define TANDO_DEMO_STORAGE_H

#include <Arduino.h>
#include <Preferences.h>

struct DemoSnapshot {
  uint32_t elapsedMs = 0;
  uint8_t  stage = 1;
  uint8_t  credits = 0;
  uint8_t  careMask = 0;
  bool     started = false;
  bool     completed = false;
  uint8_t  hungerStage = 0;
  uint8_t  hungerShown = 0;
  uint8_t  petStage = 0;
  uint8_t  petShown = 0;
};


class DemoStorage {
public:
    Preferences prefs;
    DemoStorage() {}

    void begin();
    void save(const DemoSnapshot& snapshot);
    bool load(DemoSnapshot& snapshot);
    void clear();
};

#endif
