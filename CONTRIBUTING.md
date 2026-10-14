# Contributing

Issues and pull requests are welcome.

## Firmware

```sh
pip install platformio      # or: uv tool install --python 3.12 platformio
cd firmware && pio run      # builds .pio/build/smalltv_pro/firmware.bin
../tools/flash.sh <pet-ip>  # builds and uploads over Wi-Fi
```

`tools/preview/preview.sh` renders every pet mood and body type on a Mac or Linux machine into `tools/preview/pets.png`, so art changes can be checked without flashing.

Please keep the boot path safe: Wi-Fi and `/update` come up before anything that draws. A build that crashes three times in a row boots into safe mode, which only runs those two.

## Plugin

```sh
claude --plugin-dir ./plugin          # run Claude Code with your working copy
claude plugin validate plugin
claude plugin test plugin
```

If you change the HTTP API, change both sides and the table in the README.

## Other boards

Only the SmallTV Pro (classic ESP32, 8 MB) is supported. Ports to the other SmallTV variants are welcome; [smalltv-mod](https://github.com/giovi321/smalltv-mod) has their pin maps.
