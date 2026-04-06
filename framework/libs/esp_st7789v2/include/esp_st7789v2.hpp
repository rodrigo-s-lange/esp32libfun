#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp32libfun_spi.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace esp_st7789v2 {

class DisplayLockGuard;

/// Calibrated RGB565 palette copied from the validated legacy ST7789v2 driver.
constexpr uint16_t BLACK = 0x0000;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t RED = 0xE000;
constexpr uint16_t GREEN = 0x06C0;
constexpr uint16_t LIME = 0x0750;
constexpr uint16_t BLUE = 0x0018;
constexpr uint16_t YELLOW = 0xFEC0;
constexpr uint16_t CYAN = 0x07FF;
constexpr uint16_t MAGENTA = 0xD817;
constexpr uint16_t ORANGE = 0xF400;
constexpr uint16_t PINK = 0xE0F6;
constexpr uint16_t DARK_PINK = 0xF00F;
constexpr uint16_t GOLD = 0xF680;
constexpr uint16_t GRAY = 0x738E;
constexpr uint16_t LIGHT_GRAY = 0x9CD3;
constexpr uint16_t DARK_GRAY = 0x528A;
constexpr uint16_t NAVY = 0x0010;
constexpr uint16_t DEEP_BLUE = 0x0014;
constexpr uint16_t SKY_BLUE = 0x05DF;
constexpr uint16_t TEAL = 0x0451;
constexpr uint16_t MAROON = 0x7800;
constexpr uint16_t OLIVE = 0x7C40;

/// `ROTATION_0` matches the validated hardware preset ported from the older driver.
enum St7789v2Rotation {
    ST7789V2_ROTATION_0 = 0,
    ST7789V2_ROTATION_90,
    ST7789V2_ROTATION_180,
    ST7789V2_ROTATION_270,
};

enum St7789v2Align {
    ST7789V2_ALIGN_LEFT = 0,
    ST7789V2_ALIGN_CENTER,
    ST7789V2_ALIGN_RIGHT,
};

struct St7789v2TextBoxState {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int scale = 1;
    uint16_t fg = WHITE;
    uint16_t bg = BLACK;
    St7789v2Align align = ST7789V2_ALIGN_LEFT;
    bool initialized = false;
    char last_text[64] = {};
};

struct St7789v2ProgressBarState {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int min = 0;
    int max = 100;
    int value = 0;
    uint16_t border_color = WHITE;
    uint16_t fill_color = GREEN;
    uint16_t bg_color = BLACK;
    bool initialized = false;
};

struct St7789v2SevenSegBoxState {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int thickness = 4;
    uint16_t fg = WHITE;
    uint16_t bg = BLACK;
    St7789v2Align align = ST7789V2_ALIGN_LEFT;
    bool initialized = false;
    char last_text[32] = {};
};

class St7789v2 {
public:
    static constexpr uint32_t DEFAULT_CLOCK_HZ = SPI_DISPLAY;
#if SOC_SPI_PERIPH_NUM > 2
    static constexpr int DEFAULT_SPI_PORT = SPI_HOST_3;
#else
    static constexpr int DEFAULT_SPI_PORT = SPI_HOST_DEFAULT;
#endif
    static constexpr uint16_t DEFAULT_WIDTH = 320;
    static constexpr uint16_t DEFAULT_HEIGHT = 170;
    static constexpr uint16_t DEFAULT_X_GAP = 0;
    static constexpr uint16_t DEFAULT_Y_GAP = 35;
    static constexpr size_t DEFAULT_FILL_LINES = 4;
    static constexpr size_t MAX_FILL_WIDTH = 320;

    /// Attaches one validated ST7789v2 panel to one SPI bus already started with `spi.begin()`.
    ///
    /// The application owns the SPI bus. This library owns only the display device
    /// registration on that bus plus the display-specific GPIO pins.
    ///
    /// @param cs_pin Chip-select pin already wired to the panel.
    /// @param dc_pin Data/command pin.
    /// @param rst_pin Optional reset pin, or `-1` when not connected.
    /// @param blk_pin Optional backlight pin, or `-1` when the backlight is hardwired.
    /// @param port SPI host previously started with `spi.begin()`.
    /// @param clock_hz SPI clock used for panel transfers.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t init(int cs_pin,
                   int dc_pin,
                   int rst_pin = -1,
                   int blk_pin = -1,
                   int port = DEFAULT_SPI_PORT,
                   uint32_t clock_hz = DEFAULT_CLOCK_HZ);

    /// Releases the SPI device registration and resets the instance state.
    esp_err_t end(void);
    /// Returns true when the panel was initialized successfully.
    [[nodiscard]] bool ready(void) const;

    /// Applies one of the calibrated panel rotations.
    esp_err_t setRotation(St7789v2Rotation rotation);
    /// Enables or disables panel color inversion.
    esp_err_t setInvert(bool invert);
    /// Controls the optional backlight pin.
    esp_err_t backlight(bool on);

    /// Fills the full visible screen with one RGB565 color.
    esp_err_t fillScreen(uint16_t color);
    /// Alias for `fillScreen()`.
    inline esp_err_t clear(uint16_t color = BLACK)
    {
        return fillScreen(color);
    }
    /// Fills one visible rectangle with one RGB565 color.
    esp_err_t fillRect(int x, int y, int width, int height, uint16_t color);
    /// Clears one visible area with the selected background color.
    inline esp_err_t clearArea(int x, int y, int width, int height, uint16_t color = BLACK)
    {
        return fillRect(x, y, width, height, color);
    }
    /// Draws one pixel.
    esp_err_t drawPixel(int x, int y, uint16_t color);
    /// Draws one generic line.
    esp_err_t drawLine(int x0, int y0, int x1, int y1, uint16_t color);
    /// Draws one horizontal line.
    esp_err_t drawHLine(int x, int y, int width, uint16_t color);
    /// Draws one vertical line.
    esp_err_t drawVLine(int x, int y, int height, uint16_t color);
    /// Draws one rectangular outline.
    esp_err_t drawRect(int x, int y, int width, int height, uint16_t color);
    /// Draws one rounded rectangular outline.
    esp_err_t drawRoundRect(int x, int y, int width, int height, int radius, uint16_t color);
    /// Draws one rectangular grid.
    esp_err_t drawGrid(int x, int y, int width, int height, int cols, int rows, uint16_t color);
    /// Draws one circle outline.
    esp_err_t drawCircle(int cx, int cy, int radius, uint16_t color);
    /// Draws one filled circle.
    esp_err_t fillCircle(int cx, int cy, int radius, uint16_t color);
    /// Draws one triangle outline.
    esp_err_t drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);
    /// Draws one filled triangle.
    esp_err_t fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);
    /// Draws one filled rounded rectangle.
    esp_err_t fillRoundRect(int x, int y, int width, int height, int radius, uint16_t color);
    /// Opens one partial-update window and prepares the panel for streamed pixel writes.
    ///
    /// This is the low-level path for incremental redraws. After `beginRegion()`,
    /// call `pushPixels()` and/or `pushColor()` until the rectangle is fully sent,
    /// then finish with `endRegion()`.
    ///
    /// Pixels are written in row-major order, left-to-right and top-to-bottom.
    ///
    /// @param x Visible X coordinate.
    /// @param y Visible Y coordinate.
    /// @param width Region width in pixels.
    /// @param height Region height in pixels.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t beginRegion(int x, int y, int width, int height);
    /// Streams one RGB565 pixel buffer into the currently active region.
    ///
    /// The buffer must contain `pixel_count` pixels in host-endian `uint16_t`
    /// RGB565 format. The library converts them to the panel wire format.
    ///
    /// `pushPixels()` can be called multiple times after one `beginRegion()`,
    /// which makes it suitable for tile-based redraw, sprites, and line-by-line
    /// UI updates without touching the rest of the screen.
    ///
    /// @param pixels Pointer to the source RGB565 pixels.
    /// @param pixel_count Number of pixels to write from `pixels`.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t pushPixels(const uint16_t *pixels, size_t pixel_count);
    /// Streams one repeated RGB565 color into the currently active region.
    ///
    /// This is useful for clearing or filling only part of the active window
    /// without rebuilding a full temporary pixel buffer in the application.
    ///
    /// @param color RGB565 color to repeat.
    /// @param pixel_count Number of pixels to emit.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t pushColor(uint16_t color, size_t pixel_count);
    /// Closes the current partial-update region.
    ///
    /// This resets the internal stream state. It is valid to call `endRegion()`
    /// even when no region is active.
    esp_err_t endRegion(void);
    /// Writes one full RGB565 rectangle in a single call.
    ///
    /// This is the simplest public API for updating only one sector of the
    /// display from a framebuffer, image, or tile.
    ///
    /// @param x Visible X coordinate.
    /// @param y Visible Y coordinate.
    /// @param width Region width in pixels.
    /// @param height Region height in pixels.
    /// @param pixels Pointer to `width * height` RGB565 pixels.
    /// @param pixel_count Number of pixels available in `pixels`.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t writeRegion(int x, int y, int width, int height, const uint16_t *pixels, size_t pixel_count);
    /// Fills one full rectangle in a single call using the partial-update path.
    ///
    /// This is similar to `fillRect()`, but goes through the same public region
    /// streaming API used by higher-level incremental redraw code.
    esp_err_t fillRegion(int x, int y, int width, int height, uint16_t color);

    /// Draws one 5x7 font glyph.
    esp_err_t drawChar(int x, int y, char c, uint16_t fg, uint16_t bg = BLACK, int scale = 1);
    /// Draws one 5x7 text string.
    esp_err_t drawText(int x, int y, const char *text, uint16_t fg, uint16_t bg = BLACK, int scale = 1);
    /// Draws one text line aligned against the current visible width.
    esp_err_t drawTextAligned(int y,
                              const char *text,
                              uint16_t fg,
                              uint16_t bg = BLACK,
                              int scale = 1,
                              St7789v2Align align = ST7789V2_ALIGN_LEFT);
    /// Clears one box and draws aligned text inside it.
    esp_err_t drawTextBox(int x,
                          int y,
                          int width,
                          int height,
                          const char *text,
                          uint16_t fg,
                          uint16_t bg = BLACK,
                          int scale = 1,
                          St7789v2Align align = ST7789V2_ALIGN_LEFT);
    /// Draws one glyph in the custom 7-segment style.
    esp_err_t draw7SegChar(int x, int y, char c, int height, int thickness, uint16_t fg, uint16_t bg = BLACK);
    /// Draws a string in the custom 7-segment style.
    esp_err_t draw7SegText(int x, int y, const char *text, int height, int thickness, uint16_t fg, uint16_t bg = BLACK);
    /// Clears one box and redraws aligned 7-segment text inside it.
    esp_err_t draw7SegBox(int x,
                          int y,
                          int width,
                          int height,
                          int thickness,
                          const char *text,
                          uint16_t fg,
                          uint16_t bg = BLACK,
                          St7789v2Align align = ST7789V2_ALIGN_LEFT);
    /// Redraws the 7-segment box only when the content changed.
    esp_err_t update7SegBoxIfChanged(St7789v2SevenSegBoxState *box, const char *text);
    /// Redraws the text box only when the content changed.
    esp_err_t updateTextBoxIfChanged(St7789v2TextBoxState *box, const char *text);
    /// Draws one simple horizontal progress bar.
    esp_err_t drawProgressBar(int x,
                              int y,
                              int width,
                              int height,
                              int min,
                              int max,
                              int value,
                              uint16_t border_color,
                              uint16_t fill_color,
                              uint16_t bg_color);
    /// Redraws the progress bar only when the numeric value changed.
    esp_err_t updateProgressBarIfChanged(St7789v2ProgressBarState *bar, int value);

    /// Returns the width in pixels of one 5x7 string at the selected scale.
    [[nodiscard]] int textWidth(const char *text, int scale = 1) const;
    /// Returns the width in pixels of one 7-segment string.
    [[nodiscard]] int sevenSegTextWidth(const char *text, int height, int thickness) const;

    [[nodiscard]] uint16_t width(void) const;
    [[nodiscard]] uint16_t height(void) const;
    [[nodiscard]] uint16_t xGap(void) const;
    [[nodiscard]] uint16_t yGap(void) const;
    [[nodiscard]] int csPin(void) const;
    [[nodiscard]] int dcPin(void) const;
    [[nodiscard]] int rstPin(void) const;
    [[nodiscard]] int blkPin(void) const;
    [[nodiscard]] int port(void) const;
    [[nodiscard]] uint32_t clockHz(void) const;
    [[nodiscard]] St7789v2Rotation rotation(void) const;
    [[nodiscard]] bool inverted(void) const;
    [[nodiscard]] bool hasBacklight(void) const;
    [[nodiscard]] bool backlight(void) const;
    /// Returns true while a partial-update region is active.
    [[nodiscard]] bool regionActive(void) const;
    /// Returns how many pixels are still expected in the active region.
    [[nodiscard]] size_t regionPixelsRemaining(void) const;

private:
    friend class DisplayLockGuard;

    static constexpr size_t FILL_BUFFER_PIXELS = MAX_FILL_WIDTH * DEFAULT_FILL_LINES;

    esp_err_t ensureMutex(void);
    bool lock(void) const;
    void unlock(void) const;
    esp_err_t ensureReadyLocked(void) const;
    static TickType_t msToTicks(uint32_t ms);
    esp_err_t resetLocked(void);
    esp_err_t sendCommandLocked(uint8_t cmd, const uint8_t *data = nullptr, size_t len = 0);
    esp_err_t sendDataLocked(const uint8_t *data, size_t len);
    esp_err_t initSequenceLocked(void);
    esp_err_t applyRotationLocked(St7789v2Rotation rotation);
    esp_err_t setWindowLocked(int x, int y, int width, int height);
    esp_err_t beginRegionLocked(int x, int y, int width, int height);
    esp_err_t pushPixelsLocked(const uint16_t *pixels, size_t pixel_count);
    esp_err_t pushColorLocked(uint16_t color, size_t pixel_count);
    esp_err_t endRegionLocked(void);
    esp_err_t fillRectLocked(int x, int y, int width, int height, uint16_t color);
    esp_err_t drawPixelLocked(int x, int y, uint16_t color);
    esp_err_t drawLineLocked(int x0, int y0, int x1, int y1, uint16_t color);
    esp_err_t drawHLineLocked(int x, int y, int width, uint16_t color);
    esp_err_t drawVLineLocked(int x, int y, int height, uint16_t color);
    esp_err_t drawRectLocked(int x, int y, int width, int height, uint16_t color);
    esp_err_t drawRoundRectLocked(int x, int y, int width, int height, int radius, uint16_t color);
    esp_err_t drawGridLocked(int x, int y, int width, int height, int cols, int rows, uint16_t color);
    esp_err_t drawCircleLocked(int cx, int cy, int radius, uint16_t color);
    esp_err_t fillCircleLocked(int cx, int cy, int radius, uint16_t color);
    esp_err_t drawTriangleLocked(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);
    esp_err_t fillTriangleLocked(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);
    esp_err_t fillRoundRectLocked(int x, int y, int width, int height, int radius, uint16_t color);
    esp_err_t drawCharLocked(int x, int y, char c, uint16_t fg, uint16_t bg, int scale);
    esp_err_t drawTextLocked(int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale);
    esp_err_t drawTextBoxLocked(int x, int y, int width, int height, const char *text, uint16_t fg, uint16_t bg, int scale, St7789v2Align align);
    esp_err_t draw7SegCharLocked(int x, int y, char c, int height, int thickness, uint16_t fg, uint16_t bg);
    esp_err_t draw7SegTextLocked(int x, int y, const char *text, int height, int thickness, uint16_t fg, uint16_t bg);
    esp_err_t draw7SegBoxLocked(int x, int y, int width, int height, int thickness, const char *text, uint16_t fg, uint16_t bg, St7789v2Align align);
    esp_err_t drawProgressBarLocked(int x, int y, int width, int height, int min, int max, int value, uint16_t border_color, uint16_t fill_color, uint16_t bg_color);
    void resetStateLocked(void);

    bool configured_ = false;
    bool invert_ = true;
    bool backlight_on_ = false;
    int cs_pin_ = -1;
    int dc_pin_ = -1;
    int rst_pin_ = -1;
    int blk_pin_ = -1;
    int port_ = DEFAULT_SPI_PORT;
    uint32_t clock_hz_ = DEFAULT_CLOCK_HZ;
    uint16_t width_ = DEFAULT_WIDTH;
    uint16_t height_ = DEFAULT_HEIGHT;
    uint16_t x_gap_ = DEFAULT_X_GAP;
    uint16_t y_gap_ = DEFAULT_Y_GAP;
    St7789v2Rotation rotation_ = ST7789V2_ROTATION_0;
    bool region_active_ = false;
    size_t region_pixels_remaining_ = 0;

    SemaphoreHandle_t mutex_ = nullptr;
    StaticSemaphore_t mutex_storage_ {};
    uint16_t fill_buffer_[FILL_BUFFER_PIXELS] = {};
};

extern St7789v2 st7789v2;

} // namespace esp_st7789v2

using esp_st7789v2::St7789v2;
using esp_st7789v2::st7789v2;
using esp_st7789v2::St7789v2Rotation;
using esp_st7789v2::St7789v2Align;
using esp_st7789v2::St7789v2TextBoxState;
using esp_st7789v2::St7789v2ProgressBarState;
using esp_st7789v2::St7789v2SevenSegBoxState;
using esp_st7789v2::BLACK;
using esp_st7789v2::WHITE;
using esp_st7789v2::RED;
using esp_st7789v2::GREEN;
using esp_st7789v2::LIME;
using esp_st7789v2::BLUE;
using esp_st7789v2::YELLOW;
using esp_st7789v2::CYAN;
using esp_st7789v2::MAGENTA;
using esp_st7789v2::ORANGE;
using esp_st7789v2::PINK;
using esp_st7789v2::DARK_PINK;
using esp_st7789v2::GOLD;
using esp_st7789v2::GRAY;
using esp_st7789v2::LIGHT_GRAY;
using esp_st7789v2::DARK_GRAY;
using esp_st7789v2::NAVY;
using esp_st7789v2::DEEP_BLUE;
using esp_st7789v2::SKY_BLUE;
using esp_st7789v2::TEAL;
using esp_st7789v2::MAROON;
using esp_st7789v2::OLIVE;
using esp_st7789v2::ST7789V2_ROTATION_0;
using esp_st7789v2::ST7789V2_ROTATION_90;
using esp_st7789v2::ST7789V2_ROTATION_180;
using esp_st7789v2::ST7789V2_ROTATION_270;
using esp_st7789v2::ST7789V2_ALIGN_LEFT;
using esp_st7789v2::ST7789V2_ALIGN_CENTER;
using esp_st7789v2::ST7789V2_ALIGN_RIGHT;
