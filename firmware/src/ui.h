// ui.h: lays out the 240x240 screen around the pet.
//
//   0..23    header: session name, one dot per live session
//   26..157  the pet (160x132 canvas, 32x32 grid at 4x)
//   158..181 subagent helpers
//   182..199 narration, scrolling when long
//   202..223 context / 5-hour / 7-day bars
//   (an approval replaces 158..227 with what is being asked)
// Nothing below y=228: the case's bezel covers the panel's bottom rows.
#pragma once
#include <Arduino.h>
#include "model.h"

namespace ui {
  bool begin();      // false when the canvases could not be allocated
  void render(const View& v, uint32_t now);
  void pet();        // the button was tapped outside an approval
  void invalidate(); // redraw everything on the next frame

  // The session picker (double tap): tap moves the highlight, a hold or two
  // seconds without a tap picks it.
  bool isPickerOpen();
  void openPicker(const View& v);
  void pickerNext(const View& v);
  bool isPickDue();
  String pickerChoice(const View& v);   // closes it; "" means Auto
  void closePicker();
}
