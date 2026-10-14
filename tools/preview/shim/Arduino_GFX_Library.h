// A framebuffer with just the drawing calls pet.cpp makes (preview only).
#pragma once
#include "Arduino.h"
class Arduino_GFX {
 public:
  int w, h; uint16_t* fb;
  Arduino_GFX(int w_, int h_) : w(w_), h(h_), fb(new uint16_t[w_ * h_]()) {}
  void fillRect(int x, int y, int rw, int rh, uint16_t c) {
    for (int j = y; j < y + rh; j++) for (int i = x; i < x + rw; i++)
      if (i >= 0 && j >= 0 && i < w && j < h) fb[j * w + i] = c;
  }
};
