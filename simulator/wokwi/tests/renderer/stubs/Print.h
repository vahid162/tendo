#pragma once
#include "Arduino.h"
class Print {
 public:
  virtual size_t write(uint8_t) = 0;
  size_t write(const char *text) { size_t size = 0; while (*text) size += write(*text++); return size; }
  size_t print(const char *text) { return write(text); }
};
