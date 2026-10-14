#include "net.h"
#include "board.h"
#include "display.h"
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

namespace {

DNSServer g_dns;
bool      g_isHotspot = false;
bool      g_isMdnsUp = false;
String    g_ssid, g_pass;
uint32_t  g_hotspotSinceMs = 0;
uint32_t  g_connectedSinceMs = 0;

void loadCredentials() {
  Preferences p;
  p.begin("net", true);
  g_ssid = p.getString("ssid", "");
  g_pass = p.getString("pass", "");
  p.end();
#ifdef WIFI_SSID
  if (g_ssid.isEmpty()) {
    g_ssid = WIFI_SSID;
#ifdef WIFI_PASS
    g_pass = WIFI_PASS;
#endif
  }
#endif
}

void startMdns() {
  if (g_isMdnsUp) return;
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "app", "claude-pet");
    g_isMdnsUp = true;
  }
}

void startHotspot() {
  WiFi.mode(g_ssid.isEmpty() ? WIFI_AP : WIFI_AP_STA);
  WiFi.softAP(AP_SSID);
  g_dns.start(53, "*", WiFi.softAPIP());
  g_isHotspot = true;
  g_hotspotSinceMs = millis();
  if (!g_ssid.isEmpty()) WiFi.begin(g_ssid.c_str(), g_pass.c_str());
}

void stopHotspot() {
  g_dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  g_isHotspot = false;
}

}  // namespace

namespace net {

void begin(uint32_t waitMs) {
  loadCredentials();
  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  // Modem power saving delays packets by 50-200 ms; the pet runs on USB power.
  WiFi.setSleep(false);

  if (!g_ssid.isEmpty()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_ssid.c_str(), g_pass.c_str());
    display::message("claude pet", C_ACCENT, "Connecting to", g_ssid.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < waitMs) delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    g_connectedSinceMs = millis();
    startMdns();
  } else {
    startHotspot();
  }
}

void loop() {
  static uint32_t s_downSinceMs = 0;
  if (g_isHotspot) g_dns.processNextRequest();

  if (WiFi.status() == WL_CONNECTED) {
    s_downSinceMs = 0;
    if (!g_connectedSinceMs) g_connectedSinceMs = millis();
    startMdns();
    // Joined from the setup page: keep the hotspot a little longer so the
    // phone sees the success page, then close it.
    if (g_isHotspot && millis() - g_connectedSinceMs > 60000) stopHotspot();
    return;
  }
  g_connectedSinceMs = 0;
  // Lost the network for two minutes: offer the hotspot again.
  if (!s_downSinceMs) s_downSinceMs = millis();
  if (!g_isHotspot && millis() - s_downSinceMs > 120000) startHotspot();
}

bool isConnected() { return WiFi.status() == WL_CONNECTED; }
bool isHotspot()   { return g_isHotspot; }
String ip()        { return isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString(); }
String ssid()      { return g_ssid; }

void saveCredentials(const String& ssid, const String& pass) {
  Preferences p;
  p.begin("net", false);
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  p.end();
  g_ssid = ssid;
  g_pass = pass;
}

}  // namespace net
