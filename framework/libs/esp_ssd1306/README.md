# esp_ssd1306

SSD1306 monochrome OLED library for `esp32libfun`.

This library follows the normal `esp_*` device model:

- the application owns the shared I2C bus with `i2c.begin(...)`
- `esp_ssd1306` owns only the SSD1306 device registration and one local framebuffer
- the API is direct and synchronous, so it uses `init()` / `end()` instead of `start()` / `stop()`
- the common path stays obvious: `i2c.begin(...)`, `ssd1306.init(...)`, draw into the framebuffer, then `present()`

## Supported panel preset

Current support is intentionally narrow and validated-first:

- width: `128`
- height: `32` or `64`
- I2C address: `0x3C` or `0x3D`
- horizontal memory mode with one local page-packed framebuffer

## What `init()` does

1. Checks that `i2c.begin()` already configured the selected bus.
2. Probes the SSD1306 address.
3. Registers the device with `i2c.add()`.
4. Applies a standard SSD1306 init sequence for `128x32` or `128x64`.
5. Clears and presents the local framebuffer.

## Core API

- `init(address, port, width, height)`
- `end()`
- `ready()`
- `present()`
- `clear()`
- `fill(on)`
- `display(on)`
- `invert(enabled)`
- `contrast(value)`
- `pixel(x, y, on)`
- `hLine(x0, x1, y, on)`
- `vLine(x, y0, y1, on)`
- `line(x0, y0, x1, y1, on)`
- `rect(x, y, width, height, on)`
- `fillRect(x, y, width, height, on)`
- `bitmap(x, y, width, height, data, len)`

## Text and UI helpers

The library now exposes the same higher-level concepts already used by the
larger display drivers in the framework, but in monochrome form:

- `drawChar(...)`
- `drawText(...)`
- `drawTextAligned(...)`
- `drawTextBox(...)`
- `textWidth(...)`
- `draw7SegChar(...)`
- `draw7SegText(...)`
- `draw7SegBox(...)`
- `sevenSegTextWidth(...)`
- `drawProgressBar(...)`
- `updateTextBoxIfChanged(...)`
- `update7SegBoxIfChanged(...)`
- `updateProgressBarIfChanged(...)`

This is enough for:

- small dashboards
- sensor panels
- status pages
- boot screens
- HMI mockups on cheap I2C OLED modules

## Usage

```cpp
#include "esp32libfun.hpp"
#include "esp_ssd1306.hpp"

constexpr int sdaPin = 8;
constexpr int sclPin = 9;

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(i2c.begin(sdaPin, sclPin, I2C_FAST));
    ESP_ERROR_CHECK(ssd1306.init());

    ssd1306.clear();
    ssd1306.drawTextBox(0, 0, 128, 10, "SSD1306", true, false, 1, SSD1306_ALIGN_CENTER);
    ssd1306.draw7SegBox(0, 14, 64, 24, 3, "24", true, false, SSD1306_ALIGN_CENTER);
    ssd1306.drawProgressBar(8, 50, 112, 8, 0, 100, 72, true, true, false);
    ESP_ERROR_CHECK(ssd1306.present());
}
```

## Examples

- `docs/examples/esp_ssd1306_quick_test.cpp`
- `docs/examples/esp_ssd1306_dashboard.cpp`

## Common bicolor modules

Many cheap `128x64` SSD1306 modules use a bicolor panel:

- `y=0..15`: yellow
- `y=16..63`: blue

That split is not a driver rule, so `esp_ssd1306` keeps the framebuffer generic.
When you want a layout that respects the panel colors, use the dashboard example
as reference and place headers in the top quarter.
