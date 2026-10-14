#include "display.h"
#include "board.h"
#include <SPI.h>

namespace {

// The SmallTV's ST7789 has CS tied to GND and only latches SPI in mode 3
// (from smalltv-mod's Gfx.cpp, WTFPL). On the ESP32 the base class already
// picks mode 3; forcing it keeps that true across library versions.
class SmallTvSt7789 : public Arduino_ST7789 {
 public:
  using Arduino_ST7789::Arduino_ST7789;
  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    _override_datamode = SPI_MODE3;
    return Arduino_TFT::begin(speed);
  }
};

Arduino_DataBus* g_bus = nullptr;
Arduino_GFX*     g_gfx = nullptr;
uint8_t          g_brightness = 80;

}  // namespace

namespace display {

void begin() {
  // Backlight first: a lit but blank screen then means the panel init failed,
  // a dark one that the sketch never got this far.
  pinMode(TFT_BL, OUTPUT);
  setBrightness(g_brightness);

  g_bus = new Arduino_HWSPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED, &SPI);
  // IPS panel; the 240x320 controller RAM needs a row offset of 80 only for
  // rotations 2/3, which this firmware does not use.
  g_gfx = new SmallTvSt7789(g_bus, TFT_RST, 0, true, TFT_W, TFT_H, 0, 0, 0, 80);
  g_gfx->begin();
  g_gfx->setTextWrap(false);
  g_gfx->fillScreen(C_BG);
}

Arduino_GFX* gfx() { return g_gfx; }

void setBrightness(uint8_t pct) {
  if (pct > 100) pct = 100;
  g_brightness = pct;
  analogWrite(TFT_BL, 255 - (int)pct * 255 / 100);   // active-low
}

uint8_t brightness() { return g_brightness; }

int textWidth(const char* s, uint8_t size) { return (int)strlen(s) * 6 * size; }

void centered(const char* s, int y, uint8_t size, uint16_t fg, uint16_t bg) {
  int x = (TFT_W - textWidth(s, size)) / 2;
  g_gfx->setTextSize(size);
  g_gfx->setTextColor(fg, bg);
  g_gfx->setCursor(x < 0 ? 0 : x, y);
  g_gfx->print(s);
}

void message(const char* title, uint16_t titleColor, const char* l1, const char* l2,
             const char* l3) {
  g_gfx->fillScreen(C_BG);
  centered(title, 70, 3, titleColor, C_BG);
  if (l1) centered(l1, 120, 2, C_TEXT, C_BG);
  if (l2) centered(l2, 150, 2, C_DIM, C_BG);
  if (l3) centered(l3, 180, 2, C_DIM, C_BG);
}

}  // namespace display
