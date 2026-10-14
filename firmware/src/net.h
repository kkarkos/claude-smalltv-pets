// net.h: Wi-Fi station with a setup hotspot fallback, mDNS, captive DNS.
#pragma once
#include <Arduino.h>

namespace net {
  // Connects with the saved (or built-in) credentials, waiting up to
  // `waitMs`; opens the setup hotspot when that fails or none are saved.
  void begin(uint32_t waitMs);
  void loop();

  bool isConnected();
  bool isHotspot();
  String ip();
  String ssid();

  void saveCredentials(const String& ssid, const String& pass);
}
