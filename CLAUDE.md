# CLAUDE.md

A desk pet for Claude Code on a GeekMagic SmallTV Pro. Two halves that talk HTTP over the local network:

- `firmware/`: PlatformIO, classic ESP32 (8 MB), Arduino core 3.x via the pinned pioarduino platform. Draws the pets, keeps up to six sessions, serves the API.
- `plugin/`: the `smalltv-pet` Claude Code plugin, a mod (function hooks in `hooks/register.ts`). Sends each session's snapshot to the pet; waits on the button for permission prompts.

The HTTP API table in `README.md` is the contract. A change to it changes both halves and the table.

## Commands

```sh
cd firmware && pio run                  # build
tools/flash.sh <pet-ip>                 # build + upload over Wi-Fi (/update)
tools/preview/preview.sh                # render all moods/bodies to tools/preview/pets.png
claude plugin validate plugin           # also validates the marketplace at the root: claude plugin validate .
claude plugin test plugin               # plugin tests (tests/pet.test.ts)
```

Release: add a `## x.y.z (date)` section to `CHANGELOG.md`, then `git tag vx.y.z && git push --tags`. The workflow builds the firmware with `FW_VERSION` from the tag and creates the release with that changelog section as notes.

## Firmware rules

- **Keep the way back open.** Wi-Fi and `/update` come up before anything that draws. Three crashes in a row boot safe mode (Wi-Fi + `/update` only). A device without USB data can only be recovered over the air, so never move work ahead of `web::begin()` that could crash.
- The partition table must match the stock firmware's; an OTA update cannot change it.
- Check pet art with `tools/preview/preview.sh` before flashing. It compiles `pet.cpp` on the host against the shims in `tools/preview/shim/`, so `pet.cpp` must only use what those shims provide.
- The pixel font is ASCII only. Text from the plugin goes through `copyText()` in `model.cpp`, which turns UTF-8 into ASCII. Never copy plugin text with `copyStr()`.
- A heartbeat must not count as activity: only a new state or a new label moves `activityMs` (Auto focus and screen dimming depend on it).
- Web handlers run on the async_tcp task and the render loop on the Arduino task: everything in `model` goes through its lock, and the render loop works on a `View` copy.
- `WebServer.h` (the blocking approval server on port 81, `wait_server.cpp`) and `ESPAsyncWebServer.h` both define `HTTP_GET`: never include both in one file.
- ESPAsyncWebServer's chunked `RESPONSE_TRY_AGAIN` long-poll closes the connection after the headers on this library version; that is why approval waits use the port-81 server.
- Touch on GPIO 32 reads ~1590 untouched and ~1300-1420 touched (an 11-19 % drop). The thresholds in `touch.cpp` are set from those numbers.

## Plugin rules

- `$` is only ever spelled `$.noun.method(...)` at the call site: never stored, passed or returned (`claude plugin validate` refuses it). Helpers take closures instead, e.g. `(url, init) => $.http.fetch(url, init)`.
- A hook's own time budget is 10 s and `$.clock.sleep` counts toward it; a `$` call in flight does not. Long waits are long-polls (`$.http.fetch`), not sleeps.
- Hooks only record into the snapshot; the 200 ms flush timer sends it. Only the permission request waits on the network.
- Type-check with TypeScript 5.4+ against the types the engine lays in `plugin/.claude-plugin/types/` (gitignored).

## Build environment gotchas

- PlatformIO on Python 3.14 fails; install it on 3.12 (`uv tool install --python 3.12 platformio`).
- On a Mac whose `uv` is an Intel build, pioarduino's Python setup tries to compile the newest `cryptography` from source. `tools/flash.sh` sets `UV_CONSTRAINT` to `cryptography<47` to get a prebuilt wheel.

`CLAUDE.local.md` (gitignored) holds machine-specific notes, such as the address of your own pet.
