# Credits

## smalltv-mod

The board facts for the GeekMagic SmallTV Pro come from [giovi321/smalltv-mod](https://github.com/giovi321/smalltv-mod), which verified them on hardware:

- the pin map (`firmware/src/board.h`)
- the ST7789 panel quirks: CS tied to ground, SPI mode 3, the 80-row offset (`firmware/src/display.cpp`)
- the flash partition table that matches the stock firmware (`firmware/partitions.csv`)

smalltv-mod is released under the WTFPL:

```
            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
                    Version 2, December 2004

 Everyone is permitted to copy and distribute verbatim or modified
 copies of this license document, and changing it is allowed as long
 as the name is changed.

            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
   TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION

  0. You just DO WHAT THE FUCK YOU WANT TO.
```

## Inspiration

- [lakmadev/pixel-pet](https://github.com/lakmadev/pixel-pet): a pixel pet above the Claude Code prompt
- [lcoutodemos/chums](https://github.com/lcoutodemos/chums): Claude Code agents as characters on an Apple Watch

## Libraries

[Arduino_GFX](https://github.com/moononournation/Arduino_GFX), [ArduinoJson](https://arduinojson.org), [ESPAsyncWebServer and AsyncTCP](https://github.com/ESP32Async), the [pioarduino](https://github.com/pioarduino/platform-espressif32) ESP32 platform.

GeekMagic and SmallTV are trademarks of their owner. This project is not affiliated with GeekMagic or Anthropic.
