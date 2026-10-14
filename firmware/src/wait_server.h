// wait_server.h: a blocking HTTP server on port 81, in its own task, for
// approval long-polls: it holds a request until the button answers without
// stalling the async server on port 80 or the render loop. (The async
// server's chunked "try again" responses close the connection after the
// headers on this library version.) Kept in its own file because WebServer.h
// and ESPAsyncWebServer.h both define HTTP_GET.
#pragma once

void startWaitServer();
