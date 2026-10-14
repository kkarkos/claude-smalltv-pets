// touch.h: the capacitive pad on top of the case, turned into gestures.
#pragma once
#include <Arduino.h>

enum class Gesture : uint8_t { None, Tap, DoubleTap, Hold };

namespace touch {
  void begin();
  // Call often (every loop). `isQuick` reports a Tap at release without
  // waiting to see whether a second tap follows (used while an approval is
  // pending, where a tap must answer at once).
  Gesture poll(bool isQuick);
  bool  isDown();
  float holdProgress();     // 0..1 while held, toward a Hold
  uint16_t raw();
  uint16_t baseline();
}
