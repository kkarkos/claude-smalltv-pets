// display.h: the ST7789 panel, its backlight and a few text helpers.
#pragma once
#include <Arduino_GFX_Library.h>

namespace display {
  void begin();
  Arduino_GFX* gfx();
  void setBrightness(uint8_t pct);   // 0..100
  uint8_t brightness();

  // Built-in 6x8 font, integer scaled.
  int  textWidth(const char* s, uint8_t size);
  void centered(const char* s, int y, uint8_t size, uint16_t fg, uint16_t bg);
  // A full-screen message, for boot, setup and update screens.
  void message(const char* title, uint16_t titleColor, const char* l1 = nullptr,
               const char* l2 = nullptr, const char* l3 = nullptr);
}

// RGB565 helpers
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t C_BG      = rgb(14, 12, 20);
constexpr uint16_t C_PANEL   = rgb(30, 27, 40);
constexpr uint16_t C_TEXT    = rgb(236, 232, 224);
constexpr uint16_t C_DIM     = rgb(130, 124, 140);
constexpr uint16_t C_ACCENT  = rgb(217, 119, 87);   // Claude-ish orange
constexpr uint16_t C_GOOD    = rgb(110, 210, 120);
constexpr uint16_t C_WARN    = rgb(240, 190, 70);
constexpr uint16_t C_BAD     = rgb(235, 90, 80);
