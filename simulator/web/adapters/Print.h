#pragma once
#include "Arduino.h"
#include <cstdio>
#include <type_traits>
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;

  virtual size_t write(const uint8_t *data, size_t count) {
    size_t written = 0;
    while (written < count) {
      if (!write(data[written])) break;
      ++written;
    }
    return written;
  }

  size_t write(const char *text) {
    return write(reinterpret_cast<const uint8_t *>(text), strlen(text));
  }

  size_t print(char value) { return write(uint8_t(value)); }
  size_t print(const char *text) { return write(text); }
  size_t print(const String &text) { return write(text.c_str()); }
  size_t print(const __FlashStringHelper *text) {
    return write(reinterpret_cast<const char *>(text));
  }

  size_t print(double value, int digits = 2) {
    char buffer[96];
    snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
    return write(buffer);
  }

  template <class T,
            typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  size_t print(T value, int base = 10) {
    char b[48];
    if (base == 16) snprintf(b, sizeof(b), "%lX", static_cast<unsigned long>(value));
    else if (std::is_signed<T>::value) snprintf(b, sizeof(b), "%ld", static_cast<long>(value));
    else snprintf(b, sizeof(b), "%lu", static_cast<unsigned long>(value));
    return write(b);
  }

  size_t println() { return write("\n"); }

  template <class T> size_t println(T value) {
    size_t count = print(value);
    return count + println();
  }

  template <class T> size_t println(T value, int extra) {
    size_t count = print(value, extra);
    return count + println();
  }
};
