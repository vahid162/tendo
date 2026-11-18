#pragma once
#include "Print.h"
#include <cstdarg>
class PreviewSerial : public Print {
 public:
  using Print::write;
  void begin(unsigned long) {}
  int available() const;
  int read();
  void push(int ascii);
  size_t write(uint8_t value) override;
  size_t write(const uint8_t *data, size_t count) override;
  int printf(const char *format, ...);
};
extern PreviewSerial Serial;
