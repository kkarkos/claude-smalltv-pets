#include "web.h"
#include "wait_server.h"
#include "board.h"
#include "display.h"
#include "model.h"
#include "net.h"
#include "touch.h"
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <WiFi.h>

namespace {

AsyncWebServer g_server(80);


volatile bool     g_isUpdating = false;
volatile size_t   g_updateTotal = 0;
volatile size_t   g_updateDone = 0;
volatile uint32_t g_rebootAtMs = 0;

void scheduleReboot(uint32_t delayMs) { g_rebootAtMs = millis() + delayMs; if (!g_rebootAtMs) g_rebootAtMs = 1; }

const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>claude pet</title>
<style>body{font:15px/1.4 -apple-system,system-ui,sans-serif;background:#0e0c14;color:#ece8e0;max-width:420px;margin:24px auto;padding:0 16px}
h1{font-size:20px;color:#d97757}input,button{font:inherit;width:100%;box-sizing:border-box;padding:10px;margin:6px 0;border-radius:8px;border:1px solid #3a3446;background:#1e1b28;color:#ece8e0}
button{background:#d97757;border:0;color:#fff;cursor:pointer}a{color:#d97757}.dim{color:#827c8c}code{background:#1e1b28;padding:2px 5px;border-radius:4px}</style></head><body>)HTML";

const char WIFI_FORM[] PROGMEM = R"HTML(<h1>claude pet: Wi-Fi</h1>
<form method="post" action="/wifi"><label>Network name</label><input name="ssid" required autocapitalize="none">
<label>Password</label><input name="pass" type="password"><button>Save and connect</button></form>
<p class="dim">The pet restarts and joins this network. Its address is then shown on screen.</p>)HTML";

const char UPDATE_FORM[] PROGMEM = R"HTML(<h1>claude pet: firmware</h1>
<form method="post" action="/update" enctype="multipart/form-data"><input type="file" name="firmware" accept=".bin" required>
<button>Upload</button></form><p class="dim">Or: <code>curl -F firmware=@firmware.bin http://claude-pet.local/update</code></p>)HTML";

String page(const String& body) { return String(FPSTR(PAGE_HEAD)) + body + "</body></html>"; }

String homePage() {
  String b = "<h1>claude pet</h1><p>Firmware " FW_VERSION " &middot; " + net::ip() + "</p>";
  b += "<p><a href=\"/api/info\">/api/info</a> &middot; <a href=\"/update\">update firmware</a> &middot; <a href=\"/wifi\">Wi-Fi</a></p>";
  return page(b);
}

void sendJsonError(AsyncWebServerRequest* r, int code, const String& err) {
  r->send(code, "application/json", String("{\"ok\":false,\"error\":\"") + err + "\"}");
}


// A JSON POST endpoint: the body arrives parsed; `fn` answers it.
void jsonPost(const char* uri, std::function<void(AsyncWebServerRequest*, JsonVariant&)> fn) {
  auto* h = new AsyncCallbackJsonWebHandler(uri, fn);
  h->setMethod(HTTP_POST);
  h->setMaxContentLength(4096);
  g_server.addHandler(h);
}

}  // namespace

namespace web {

void begin() {
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  g_server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (net::isHotspot() && !net::isConnected()) r->send(200, "text/html", page(FPSTR(WIFI_FORM)));
    else r->send(200, "text/html", homePage());
  });

  g_server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "text/html", page(FPSTR(WIFI_FORM)));
  });

  g_server.on("/wifi", HTTP_POST, [](AsyncWebServerRequest* r) {
    String ssid = r->hasParam("ssid", true) ? r->getParam("ssid", true)->value() : "";
    String pass = r->hasParam("pass", true) ? r->getParam("pass", true)->value() : "";
    if (ssid.isEmpty()) { r->send(400, "text/plain", "ssid is required"); return; }
    net::saveCredentials(ssid, pass);
    r->send(200, "text/html", page("<h1>Saved</h1><p>Restarting and joining <b>" + ssid +
                                   "</b>. Watch the pet's screen for its address.</p>"));
    scheduleReboot(1500);
  });

  // ---- API for the Claude Code plugin ------------------------------------
  g_server.on("/api/info", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "application/json", model::infoJson());
  });

  g_server.on("/api/touch", HTTP_GET, [](AsyncWebServerRequest* r) {
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"raw\":%u,\"baseline\":%u,\"down\":%s,\"digital\":%d}", touch::raw(),
             touch::baseline(), touch::isDown() ? "true" : "false", digitalRead(TOUCH_PIN));
    r->send(200, "application/json", buf);
  });

  jsonPost("/api/session", [](AsyncWebServerRequest* r, JsonVariant& body) {
    String err;
    if (!model::upsertSession(body, err)) return sendJsonError(r, 400, err);
    r->send(200, "application/json", "{\"ok\":true}");
  });

  jsonPost("/api/end", [](AsyncWebServerRequest* r, JsonVariant& body) {
    model::endSession(body["sid"] | "");
    r->send(200, "application/json", "{\"ok\":true}");
  });

  jsonPost("/api/approve", [](AsyncWebServerRequest* r, JsonVariant& body) {
    String err;
    if (!model::openApproval(body, err)) return sendJsonError(r, 400, err);
    r->send(200, "application/json", "{\"ok\":true}");
  });

  jsonPost("/api/cancel", [](AsyncWebServerRequest* r, JsonVariant& body) {
    model::cancelApproval(body["id"] | "");
    r->send(200, "application/json", "{\"ok\":true}");
  });

  jsonPost("/api/config", [](AsyncWebServerRequest* r, JsonVariant& body) {
    if (body["brightness"].is<int>()) display::setBrightness(body["brightness"].as<int>());
    r->send(200, "application/json", "{\"ok\":true}");
  });

  // One look at an approval's answer; the plugin waits on port 81 instead.
  g_server.on("/api/approval", HTTP_GET, [](AsyncWebServerRequest* r) {
    String id = r->hasParam("id") ? r->getParam("id")->value() : "";
    r->send(200, "application/json", String("{\"decision\":\"") + decisionName(model::decisionFor(id.c_str())) + "\"}");
  });

  g_server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* r) {
    r->send(200, "application/json", "{\"ok\":true}");
    scheduleReboot(500);
  });

  // ---- over-the-air update -------------------------------------------------
  g_server.on("/update", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "text/html", page(FPSTR(UPDATE_FORM)));
  });

  g_server.on(
      "/update", HTTP_POST,
      [](AsyncWebServerRequest* r) {
        bool ok = !Update.hasError() && Update.isFinished();
        AsyncWebServerResponse* resp =
            r->beginResponse(ok ? 200 : 500, "text/plain", ok ? "OK, restarting\n" : String("Update failed: ") + Update.errorString() + "\n");
        resp->addHeader("Connection", "close");
        r->send(resp);
        if (ok) scheduleReboot(800);
        else g_isUpdating = false;
      },
      [](AsyncWebServerRequest* r, const String& filename, size_t index, uint8_t* data, size_t len,
         bool final) {
        if (index == 0) {
          g_isUpdating = true;
          g_updateDone = 0;
          g_updateTotal = r->contentLength();
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) return;
        }
        if (Update.hasError()) return;
        if (Update.write(data, len) != len) return;
        g_updateDone = index + len;
        if (final) Update.end(true);
      });

  // Captive portal: while the hotspot is up, send every unknown URL (the
  // phone's connectivity probes) to the setup page.
  g_server.onNotFound([](AsyncWebServerRequest* r) {
    if (net::isHotspot() && !net::isConnected()) {
      r->redirect("http://" + WiFi.softAPIP().toString() + "/");
      return;
    }
    if (r->method() == HTTP_OPTIONS) { r->send(204); return; }
    r->send(404, "text/plain", "not found");
  });

  g_server.begin();
  startWaitServer();
}

bool isUpdating() { return g_isUpdating; }

int updateProgress() {
  if (!g_updateTotal) return -1;
  return (int)min((size_t)100, g_updateDone * 100 / g_updateTotal);
}

bool isRebootDue() { return g_rebootAtMs && (int32_t)(millis() - g_rebootAtMs) >= 0; }

}  // namespace web
