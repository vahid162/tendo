#pragma once
#include "Arduino.h"

extern "C" {
__attribute__((import_module("env"), import_name("preview_get_pref")))
uint32_t previewGetPref(const char *, uint32_t);
__attribute__((import_module("env"), import_name("preview_put_pref")))
void previewPutPref(const char *, uint32_t);
__attribute__((import_module("env"), import_name("preview_clear_prefs")))
void previewClearPrefs();
}

// Browser preferences model numeric values, not flash/NVS write atomicity.
class Preferences {
 public:
  bool begin(const char *, bool) { return true; }

  bool clear() {
    previewClearPrefs();
    return true;
  }

  uint32_t getUInt(const char *key, uint32_t fallback = 0) {
    return previewGetPref(key, fallback);
  }

  uint8_t getUChar(const char *key, uint8_t fallback = 0) {
    return uint8_t(previewGetPref(key, fallback));
  }

  bool getBool(const char *key, bool fallback = false) {
    return previewGetPref(key, fallback) != 0;
  }

  size_t putUInt(const char *key, uint32_t value) {
    previewPutPref(key, value);
    return 4;
  }

  size_t putUChar(const char *key, uint8_t value) {
    previewPutPref(key, value);
    return 1;
  }

  size_t putBool(const char *key, bool value) {
    previewPutPref(key, value);
    return 1;
  }
};
