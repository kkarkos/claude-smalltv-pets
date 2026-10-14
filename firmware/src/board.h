// board.h: GeekMagic SmallTV Pro pin map and panel facts.
//
// From giovi321/smalltv-mod (board_esp32_pro.h, WTFPL), verified there on
// hardware: classic ESP32, 1.54" 240x240 ST7789V on VSPI pins, CS tied to GND,
// SPI mode 3, active-low PWM backlight, capacitive touch pad on GPIO 32 (T9).
#pragma once

#define TFT_SCLK   18
#define TFT_MOSI   23
#define TFT_DC      2
#define TFT_RST     4
#define TFT_CS     -1
#define TFT_BL     25
#define TFT_W     240
#define TFT_H     240

#define TOUCH_PIN  32

#define HOSTNAME   "claude-pet"
#define AP_SSID    "ClaudePet-Setup"
// Release builds pass the tag in: -D FW_VERSION=\"0.1.0\"
#ifndef FW_VERSION
#define FW_VERSION "0.1.0-dev"
#endif
