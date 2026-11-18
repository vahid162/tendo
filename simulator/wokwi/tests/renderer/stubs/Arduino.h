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
class __FlashStringHelper;
class String : public std::string { public: using std::string::string; };
