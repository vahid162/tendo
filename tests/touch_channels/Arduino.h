#pragma once
#include <cstdint>
#include <sstream>
#include <string>

using byte = uint8_t;
constexpr int HEX = 16;
inline void delay(uint32_t) {}

struct TestSerial {
  std::string output;
  void print(const char *value) { output += value; }
  void print(char value) { output += value; }
  template <typename T> void print(T value) {
    std::ostringstream text;
    text << +value;
    output += text.str();
  }
  template <typename T> void print(T value, int base) {
    std::ostringstream text;
    if (base == HEX) text << std::hex;
    text << +value;
    output += text.str();
  }
  void println() { output += '\n'; }
  template <typename T> void println(T value) { print(value); println(); }
  template <typename T> void println(T value, int base) { print(value, base); println(); }
};
inline TestSerial Serial;
