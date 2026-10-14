#include "touch.h"
#include "board.h"

namespace {

// The classic ESP32's touch reading DROPS when the pad is touched. Measured on
// a SmallTV Pro: ~1590 untouched, ~1300-1420 touched (an 11-19% drop). Press
// and release thresholds sit apart so a reading near the edge cannot chatter.
constexpr float    PRESS_RATIO    = 0.93f;
constexpr float    RELEASE_RATIO  = 0.96f;
constexpr uint32_t SAMPLE_MS      = 15;
constexpr uint32_t HOLD_MS        = 900;
constexpr uint32_t DOUBLE_GAP_MS  = 320;
constexpr uint32_t MIN_PRESS_MS   = 30;

float    g_baseline = 0;
uint16_t g_raw = 0;
bool     g_isDown = false;
uint32_t g_downAt = 0;
bool     g_isHoldFired = false;
uint32_t g_lastSampleMs = 0;
uint32_t g_pendingTapAt = 0;   // a tap waiting to see if a second one follows

uint16_t readFiltered() {
  uint16_t a = touchRead(TOUCH_PIN), b = touchRead(TOUCH_PIN), c = touchRead(TOUCH_PIN);
  // median of three
  return max(min(a, b), min(max(a, b), c));
}

}  // namespace

namespace touch {

void begin() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) { sum += touchRead(TOUCH_PIN); delay(5); }
  g_baseline = sum / 16.0f;
}

Gesture poll(bool isQuick) {
  uint32_t now = millis();
  Gesture out = Gesture::None;

  if (g_pendingTapAt && now - g_pendingTapAt > DOUBLE_GAP_MS && !g_isDown) {
    g_pendingTapAt = 0;
    out = Gesture::Tap;
  }
  if (now - g_lastSampleMs < SAMPLE_MS) return out;
  g_lastSampleMs = now;

  g_raw = readFiltered();
  bool isTouched = g_baseline > 0 &&
                   g_raw < g_baseline * (g_isDown ? RELEASE_RATIO : PRESS_RATIO);

  if (!g_isDown) {
    // Track slow drift (temperature, humidity) only from readings near the
    // baseline, so a finger arriving slowly never drags it down.
    if (g_raw > g_baseline * 0.97f) g_baseline = g_baseline * 0.995f + g_raw * 0.005f;
    if (isTouched) {
      g_isDown = true;
      g_downAt = now;
      g_isHoldFired = false;
    }
    return out;
  }

  if (isTouched) {
    if (!g_isHoldFired && now - g_downAt >= HOLD_MS) {
      g_isHoldFired = true;
      g_pendingTapAt = 0;
      return Gesture::Hold;
    }
    return out;
  }

  // Released.
  g_isDown = false;
  uint32_t held = now - g_downAt;
  if (g_isHoldFired || held < MIN_PRESS_MS) return out;
  if (isQuick) { g_pendingTapAt = 0; return Gesture::Tap; }
  if (g_pendingTapAt) { g_pendingTapAt = 0; return Gesture::DoubleTap; }
  g_pendingTapAt = now;
  return out;
}

bool isDown() { return g_isDown; }

float holdProgress() {
  if (!g_isDown || g_isHoldFired) return 0;
  return min(1.0f, (millis() - g_downAt) / (float)HOLD_MS);
}

uint16_t raw() { return g_raw; }
uint16_t baseline() { return (uint16_t)g_baseline; }

}  // namespace touch
