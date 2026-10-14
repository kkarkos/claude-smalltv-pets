// Host stand-ins for the Arduino bits pet.cpp uses (preview only).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
using std::max; using std::min;
#ifndef PI
#define PI 3.14159265358979f
#endif
template <class T, class L, class H> T constrain(T v, L lo, H hi) { return v < lo ? lo : v > hi ? hi : v; }
class String { public: String() {} String(const char*) {} };
