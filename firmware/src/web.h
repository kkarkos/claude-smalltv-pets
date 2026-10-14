// web.h: the HTTP API the Claude Code plugin talks to, the setup page and
// the over-the-air update endpoint.
#pragma once
#include <Arduino.h>

namespace web {
  void begin();

  bool isUpdating();
  int  updateProgress();        // 0..100, -1 when the size is unknown
  bool isRebootDue();           // a reboot was requested and its delay passed
}
