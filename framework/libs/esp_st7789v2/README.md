# esp_st7789v2

Calibrated ST7789v2 driver for the validated project display.

This library follows the `esp_*` device model:
- the application owns the SPI bus with `spi.begin(...)`
- `esp_st7789v2` owns only the display device registration and display-specific GPIO pins
- the API is direct and synchronous, so it uses `init()` / `end()` instead of `start()` / `stop()`
- on targets with both user SPI hosts available, the display defaults to `SPI_HOST_3`
  so `SPI_HOST_2` can stay free for other fast peripherals such as `W5500`

## Validated panel preset

The built-in defaults were ported from the older validated C driver:
- visible size: `320 x 170`
- gap: `x = 0`, `y = 35`
- default rotation: `ST7789V2_ROTATION_0`
  this is the calibrated hardware preset inherited from the older driver
- color inversion enabled on init
- RGB565 / 16-bit color mode
- calibrated RGB565 palette reused from the validated panel driver

## What `init()` does

1. Checks that `spi.begin()` already configured the selected SPI host.
2. Registers the panel device with `spi.add()`.
3. Configures `DC`, optional `RST`, and optional `BLK`.
4. Applies the validated reset + init sequence.
5. Enables the display and turns on the optional backlight pin.

## Core API

- `init(cs, dc, rst, blk, port, clock_hz)`
- `end()`
- `setRotation(...)`
- `setInvert(...)`
- `backlight(...)`
- `fillScreen(...)`
- `fillRect(...)`
- `beginRegion(...)`
- `pushPixels(...)`
- `pushColor(...)`
- `endRegion()`
- `writeRegion(...)`
- `fillRegion(...)`
- `drawPixel(...)`
- `drawLine(...)`
- `drawRect(...)`
- `drawRoundRect(...)`
- `fillRoundRect(...)`
- `drawGrid(...)`
- `drawCircle(...)`
- `fillCircle(...)`
- `drawTriangle(...)`
- `fillTriangle(...)`
- `drawChar(...)`
- `drawText(...)`
- `drawTextAligned(...)`
- `drawTextBox(...)`
- `draw7SegChar(...)`
- `draw7SegText(...)`
- `draw7SegBox(...)`
- `update7SegBoxIfChanged(...)`
- `updateTextBoxIfChanged(...)`
- `drawProgressBar(...)`
- `updateProgressBarIfChanged(...)`

## Partial region updates

The library now exposes a public region-streaming API for updating only one
sector of the display.

This is the recommended path when:
- you already have a small RGB565 framebuffer or tile
- you want to redraw only a widget rectangle
- you want line-by-line or chunked updates without touching the rest of the screen

### One-shot region write

Use `writeRegion(...)` when you already have the whole rectangle in memory.

```cpp
uint16_t icon[32 * 32] = {};
// Fill icon[] with host-endian RGB565 pixels.

ESP_ERROR_CHECK(st7789v2.writeRegion(24, 40, 32, 32, icon, 32 * 32));
```

### Streamed region write

Use `beginRegion(...) + pushPixels(...) + endRegion()` when you want to send a
rectangle in chunks.

Pixels must be sent in row-major order:
- left to right
- top to bottom

```cpp
ESP_ERROR_CHECK(st7789v2.beginRegion(0, 0, 320, 20));

for (int line = 0; line < 20; ++line) {
    uint16_t row[320] = {};
    // Fill row[] with one scanline in RGB565.
    ESP_ERROR_CHECK(st7789v2.pushPixels(row, 320));
}

ESP_ERROR_CHECK(st7789v2.endRegion());
```

### Streamed solid-color fill

Use `pushColor(...)` when the active region should receive repeated color data
without building a temporary buffer in the application.

```cpp
ESP_ERROR_CHECK(st7789v2.beginRegion(12, 12, 100, 40));
ESP_ERROR_CHECK(st7789v2.pushColor(TEAL, 100 * 40));
ESP_ERROR_CHECK(st7789v2.endRegion());
```

### Important notes

- Input pixels are regular host-endian `uint16_t` RGB565 values.
- The library converts them to the ST7789 wire format internally.
- Any normal draw call that reconfigures the panel window implicitly cancels the current region stream.
- `regionActive()` and `regionPixelsRemaining()` can be used for diagnostics.

## Calibrated colors

The public palette now matches the older validated panel driver:

- `BLACK`, `WHITE`, `RED`, `GREEN`, `LIME`, `BLUE`
- `YELLOW`, `CYAN`, `MAGENTA`, `ORANGE`
- `PINK`, `DARK_PINK`, `GOLD`
- `GRAY`, `LIGHT_GRAY`, `DARK_GRAY`
- `NAVY`, `DEEP_BLUE`, `SKY_BLUE`, `TEAL`
- `MAROON`, `OLIVE`

## Usage

```cpp
#include "esp32libfun.hpp"
#include "esp_st7789v2.hpp"

constexpr int kPinMosi = 4;
constexpr int kPinSclk = 5;
constexpr int kPinRst = 6;
constexpr int kPinDc = 7;
constexpr int kPinCs = 15;
#if SOC_SPI_PERIPH_NUM > 2
constexpr int displaySpiPort = SPI_HOST_3;
#else
constexpr int displaySpiPort = SPI_HOST_DEFAULT;
#endif

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(spi.begin(kPinSclk, kPinMosi, -1, displaySpiPort));
    ESP_ERROR_CHECK(st7789v2.init(kPinCs, kPinDc, kPinRst, -1, displaySpiPort));

ESP_ERROR_CHECK(st7789v2.fillScreen(BLACK));
ESP_ERROR_CHECK(st7789v2.drawTextAligned(8, "ESP32LIBFUN", WHITE, BLACK, 2, ST7789V2_ALIGN_CENTER));
ESP_ERROR_CHECK(st7789v2.drawProgressBar(20, 140, 280, 16, 0, 100, 75, WHITE, GREEN, BLACK));

uint16_t tile[32 * 32] = {};
ESP_ERROR_CHECK(st7789v2.writeRegion(144, 60, 32, 32, tile, 32 * 32));
}
```

## Examples

- `docs/examples/esp_st7789v2_dashboard.cpp`
