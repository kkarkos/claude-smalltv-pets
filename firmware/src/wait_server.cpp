#include "wait_server.h"
#include "model.h"
#include <WebServer.h>

namespace {

WebServer g_server(81);

void serve(void*) {
  g_server.on("/api/approval", HTTP_GET, [] {
    String id = g_server.arg("id");
    long wait = constrain(g_server.arg("wait").toInt(), 0L, 9L);
    uint32_t until = millis() + (uint32_t)wait * 1000;
    Decision d;
    while ((d = model::decisionFor(id.c_str())) == Decision::Pending && (int32_t)(until - millis()) > 0)
      delay(40);
    g_server.sendHeader("Cache-Control", "no-store");
    g_server.sendHeader("Access-Control-Allow-Origin", "*");
    g_server.send(200, "application/json", String("{\"decision\":\"") + decisionName(d) + "\"}");
  });
  g_server.begin();
  for (;;) {
    g_server.handleClient();
    delay(5);
  }
}

}  // namespace

void startWaitServer() {
  xTaskCreatePinnedToCore(serve, "approval-wait", 6144, nullptr, 1, nullptr, 0);
}
