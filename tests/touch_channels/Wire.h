#pragma once
struct TestWire {
  int sda = -1, scl = -1;
  void begin(int data, int clock) { sda = data; scl = clock; }
};
inline TestWire Wire;
