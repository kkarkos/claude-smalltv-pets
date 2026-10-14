// claude-smalltv-pets: a desk pet for Claude Code on the GeekMagic SmallTV Pro.
//
// Boot order puts the way back first: Wi-Fi and the /update endpoint come up
// before anything that draws, and three crashes in a row start a safe mode
// with nothing but those, so a bad build can always be replaced over the air.
#include <Arduino.h>
#include <esp_ota_ops.h>
#include "board.h"
#include "display.h"
#include "model.h"
#include "net.h"
#include "touch.h"
#include "ui.h"
#include "web.h"

namespace {

constexpr uint32_t CRASH_MAGIC     = 0xC1A0DE01;
constexpr uint32_t STABLE_AFTER_MS = 20000;
constexpr uint32_t FRAME_MS        = 66;          // ~15 fps
constexpr uint32_t DIM_AFTER_MS    = 10 * 60 * 1000;

RTC_NOINIT_ATTR uint32_t g_crashMagic;
RTC_NOINIT_ATTR uint32_t g_crashCount;

bool     g_isSafeMode = false;
bool     g_isMarkedStable = false;
bool     g_isDimmed = false;
uint8_t  g_brightness = 80;
uint32_t g_lastFrameMs = 0;
uint32_t g_lastTickMs = 0;
uint32_t g_lastTouchMs = 0;

bool wasCrash(esp_reset_reason_t r) {
  return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
}

bool isAskingFor(const View& v) {
  return v.hasApproval && v.hasSession && !strcmp(v.approval.sid, v.session.sid);
}

void handleGesture(Gesture g, const View& v) {
  if (ui::isPickerOpen() && ui::isPickDue()) {
    model::pin(ui::pickerChoice(v).c_str());
    ui::invalidate();
  }
  if (g == Gesture::None) return;
  g_lastTouchMs = millis();
  model::touchActivity();
  if (g_isDimmed) return;   // the first touch only wakes the screen

  if (ui::isPickerOpen()) {
    if (g == Gesture::Hold) {
      model::pin(ui::pickerChoice(v).c_str());
      ui::invalidate();
    } else {
      ui::pickerNext(v);
    }
    return;
  }

  bool isAsking = isAskingFor(v);
  switch (g) {
    case Gesture::Tap:
      if (isAsking) model::answerApproval(true);
      else ui::pet();
      break;
    case Gesture::Hold:
      if (isAsking) {
        model::answerApproval(false);
      } else {
        g_brightness = g_brightness > 50 ? 30 : g_brightness > 15 ? 8 : 80;
        display::setBrightness(g_brightness);
      }
      break;
    case Gesture::DoubleTap:
      if (!isAsking && v.count > 0) ui::openPicker(v);
      break;
    default:
      break;
  }
}

void updateDimming(uint32_t now) {
  bool isIdle = now - model::lastActivityMs() > DIM_AFTER_MS && now - g_lastTouchMs > DIM_AFTER_MS;
  if (isIdle && !g_isDimmed) { g_isDimmed = true; display::setBrightness(3); }
  if (!isIdle && g_isDimmed) { g_isDimmed = false; display::setBrightness(g_brightness); }
}

}  // namespace

// Lets the Arduino core leave a freshly flashed image unconfirmed, so a
// bootloader with rollback enabled can return to the previous image if this
// one never runs long enough to confirm itself. Without rollback in the
// bootloader this changes nothing.
extern "C" bool verifyRollbackLater() { return true; }

void setup() {
  Serial.begin(115200);

  esp_reset_reason_t reason = esp_reset_reason();
  if (g_crashMagic != CRASH_MAGIC) { g_crashMagic = CRASH_MAGIC; g_crashCount = 0; }
  g_crashCount = wasCrash(reason) ? g_crashCount + 1 : 0;
  g_isSafeMode = g_crashCount >= 3;

  display::begin();
  display::message("claude pet", C_ACCENT, "v" FW_VERSION, g_isSafeMode ? "safe mode" : nullptr);

  model::begin();
  net::begin(15000);
  web::begin();

  if (g_isSafeMode) {
    display::message("SAFE MODE", C_BAD, "crashed 3 times", net::ip().c_str(), "/update to fix");
    return;
  }

  touch::begin();
  if (!ui::begin()) {
    g_isSafeMode = true;
    display::message("NO MEMORY", C_BAD, net::ip().c_str(), "/update to fix");
    return;
  }
  if (net::isHotspot()) {
    display::message("Wi-Fi setup", C_WARN, "join network", AP_SSID, "open 192.168.4.1");
    delay(4000);
  } else {
    display::message("claude pet", C_ACCENT, net::ip().c_str(), HOSTNAME ".local");
    delay(2500);
  }
  ui::invalidate();
}

void loop() {
  uint32_t now = millis();
  net::loop();

  if (!g_isMarkedStable && now > STABLE_AFTER_MS) {
    g_isMarkedStable = true;
    g_crashCount = 0;
    esp_ota_mark_app_valid_cancel_rollback();
  }

  if (web::isRebootDue()) {
    display::message("restarting", C_ACCENT);
    delay(200);
    ESP.restart();
  }

  if (web::isUpdating()) {
    static int s_shown = -2;
    int p = web::updateProgress();
    if (p != s_shown) {
      s_shown = p;
      char line[16];
      if (p >= 0) snprintf(line, sizeof(line), "%d%%", p);
      else strcpy(line, "...");
      display::message("updating", C_WARN, line, "do not unplug");
    }
    delay(20);
    return;
  }

  if (g_isSafeMode) { delay(50); return; }

  if (now - g_lastTickMs > 250) { g_lastTickMs = now; model::tick(); }

  View v = model::view();
  // Taps answer at once (no wait for a second tap) while asking or picking.
  handleGesture(touch::poll(isAskingFor(v) || ui::isPickerOpen()), v);
  updateDimming(now);

  if (now - g_lastFrameMs >= FRAME_MS) {
    g_lastFrameMs = now;
    ui::render(v, now);
  }
  delay(2);
}
