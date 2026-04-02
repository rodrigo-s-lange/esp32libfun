#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

namespace esp_ssd1306 {

enum Ssd1306Align {
    SSD1306_ALIGN_LEFT = 0,
    SSD1306_ALIGN_CENTER,
    SSD1306_ALIGN_RIGHT,
};

struct Ssd1306TextBoxState {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int scale = 1;
    bool fg = true;
    bool bg = false;
    Ssd1306Align align = SSD1306_ALIGN_LEFT;
    char last_text[32] = {};
};

struct Ssd1306ProgressBarState {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int min = 0;
    int max = 100;
    int last_value = -2147483647;
    bool border = true;
    bool fill = true;
    bool bg = false;
};

struct Ssd1306SevenSegBoxState {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int thickness = 2;
    bool fg = true;
    bool bg = false;
    Ssd1306Align align = SSD1306_ALIGN_LEFT;
    char last_text[32] = {};
};

class Ssd1306 {
public:
    /// Default 7-bit I2C address used by most SSD1306 modules.
    static constexpr uint16_t DEFAULT_ADDRESS = 0x3C;
    /// Alternate 7-bit I2C address used by some SSD1306 modules.
    static constexpr uint16_t ALTERNATE_ADDRESS = 0x3D;
    /// Default visible width validated by the quick-test module.
    static constexpr int DEFAULT_WIDTH = 128;
    /// Default visible height validated by the quick-test module.
    static constexpr int DEFAULT_HEIGHT = 64;
    /// Maximum framebuffer size kept locally by the driver.
    static constexpr size_t MAX_FRAME_BYTES = (DEFAULT_WIDTH * DEFAULT_HEIGHT / 8);

    /// Attaches the display to one I2C bus already started with `i2c.begin()`.
    ///
    /// The application owns the bus. This library only registers the SSD1306
    /// device and manages the local framebuffer plus display commands.
    ///
    /// @param address 7-bit I2C address, usually `0x3C` or `0x3D`.
    /// @param port I2C port index matching the previous `i2c.begin()` call.
    /// @param width Visible panel width in pixels. Current support is `128`.
    /// @param height Visible panel height in pixels. Current support is `32` or `64`.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t init(uint16_t address = DEFAULT_ADDRESS,
                   int port = 0,
                   int width = DEFAULT_WIDTH,
                   int height = DEFAULT_HEIGHT);

    /// Releases the registered I2C device and clears the local state.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t end(void);

    /// Returns true when `init()` has completed successfully.
    [[nodiscard]] bool ready(void) const;

    /// Sends the current framebuffer to the panel.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t present(void);

    /// Clears the framebuffer to black.
    void clear(void);
    /// Fills the framebuffer to one solid color.
    ///
    /// @param on `true` fills white, `false` fills black.
    void fill(bool on);

    /// Turns the panel on or off without changing the framebuffer.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t display(bool on);
    /// Enables or disables panel inversion.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t invert(bool enabled);
    /// Updates the SSD1306 contrast register.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t contrast(uint8_t value);

    /// Draws or clears one pixel in the local framebuffer.
    void pixel(int x, int y, bool on = true);
    /// Draws one horizontal line in the local framebuffer.
    void hLine(int x0, int x1, int y, bool on = true);
    /// Draws one vertical line in the local framebuffer.
    void vLine(int x, int y0, int y1, bool on = true);
    /// Draws one rectangle outline in the local framebuffer.
    void rect(int x, int y, int width, int height, bool on = true);
    /// Fills one rectangle in the local framebuffer.
    void fillRect(int x, int y, int width, int height, bool on = true);
    /// Draws one Bresenham line in the local framebuffer.
    void line(int x0, int y0, int x1, int y1, bool on = true);

    /// Draws one 5x7 character.
    esp_err_t drawChar(int x, int y, char c, bool fg = true, bool bg = false, int scale = 1);
    /// Draws one 5x7 string.
    esp_err_t drawText(int x, int y, const char *text, bool fg = true, bool bg = false, int scale = 1);
    /// Draws one 5x7 string aligned against the current display width.
    esp_err_t drawTextAligned(int y,
                              const char *text,
                              bool fg = true,
                              bool bg = false,
                              int scale = 1,
                              Ssd1306Align align = SSD1306_ALIGN_LEFT);
    /// Clears one box and centers/aligned one 5x7 string inside it.
    esp_err_t drawTextBox(int x,
                          int y,
                          int width,
                          int height,
                          const char *text,
                          bool fg = true,
                          bool bg = false,
                          int scale = 1,
                          Ssd1306Align align = SSD1306_ALIGN_LEFT);
    /// Draws one monochrome 7-segment glyph.
    esp_err_t draw7SegChar(int x, int y, char c, int height, int thickness, bool fg = true, bool bg = false);
    /// Draws one monochrome 7-segment string.
    esp_err_t draw7SegText(int x, int y, const char *text, int height, int thickness, bool fg = true, bool bg = false);
    /// Clears one box and draws one aligned 7-segment string inside it.
    esp_err_t draw7SegBox(int x,
                          int y,
                          int width,
                          int height,
                          int thickness,
                          const char *text,
                          bool fg = true,
                          bool bg = false,
                          Ssd1306Align align = SSD1306_ALIGN_LEFT);
    /// Draws one simple horizontal progress bar.
    esp_err_t drawProgressBar(int x,
                              int y,
                              int width,
                              int height,
                              int min,
                              int max,
                              int value,
                              bool border = true,
                              bool fill = true,
                              bool bg = false);
    /// Redraws one text box only when the visible text changed.
    esp_err_t updateTextBoxIfChanged(Ssd1306TextBoxState *box, const char *text);
    /// Redraws one 7-segment box only when the visible text changed.
    esp_err_t update7SegBoxIfChanged(Ssd1306SevenSegBoxState *box, const char *text);
    /// Redraws one progress bar only when the value changed.
    esp_err_t updateProgressBarIfChanged(Ssd1306ProgressBarState *bar, int value);

    /// Copies one monochrome bitmap into the local framebuffer.
    ///
    /// The bitmap must be packed in page order, the same format used by the
    /// SSD1306 framebuffer: `width * height / 8` bytes, left-to-right and
    /// top-to-bottom in 8-row pages.
    esp_err_t bitmap(int x, int y, int width, int height, const uint8_t *data, size_t len);

    /// Returns the configured display width in pixels.
    [[nodiscard]] int width(void) const;
    /// Returns the configured display height in pixels.
    [[nodiscard]] int height(void) const;
    /// Returns the configured number of display pages.
    [[nodiscard]] int pages(void) const;
    /// Returns the configured 7-bit I2C address.
    [[nodiscard]] uint16_t address(void) const;
    /// Returns the configured I2C port index.
    [[nodiscard]] int port(void) const;
    /// Returns the pixel width of one 5x7 string with the selected scale.
    [[nodiscard]] int textWidth(const char *text, int scale = 1) const;
    /// Returns the pixel width of one 7-segment string.
    [[nodiscard]] int sevenSegTextWidth(const char *text, int height, int thickness) const;
    /// Returns a read-only pointer to the local framebuffer.
    [[nodiscard]] const uint8_t *buffer(void) const;
    /// Returns the number of valid bytes in the local framebuffer.
    [[nodiscard]] size_t bufferSize(void) const;

private:
    static bool isValidAddress(uint16_t address);
    static bool isSupportedGeometry(int width, int height);
    static const uint8_t *font5x7ForChar(char c);
    static int sevenSegGlyphWidth(int height, int thickness);
    static bool copyText(char *dst, size_t dst_len, const char *src);

    esp_err_t command(const uint8_t *data, size_t len) const;
    esp_err_t data(const uint8_t *data, size_t len) const;
    esp_err_t write(uint8_t control, const uint8_t *data, size_t len) const;
    void clearState(void);

    bool configured_ = false;
    uint16_t address_ = DEFAULT_ADDRESS;
    int port_ = 0;
    int width_ = DEFAULT_WIDTH;
    int height_ = DEFAULT_HEIGHT;
    int pages_ = (DEFAULT_HEIGHT / 8);
    size_t buffer_size_ = MAX_FRAME_BYTES;
    uint8_t buffer_[MAX_FRAME_BYTES] = {};
};

extern Ssd1306 ssd1306;

} // namespace esp_ssd1306

using esp_ssd1306::Ssd1306;
using esp_ssd1306::ssd1306;
using esp_ssd1306::Ssd1306Align;
using esp_ssd1306::Ssd1306TextBoxState;
using esp_ssd1306::Ssd1306ProgressBarState;
using esp_ssd1306::Ssd1306SevenSegBoxState;
using esp_ssd1306::SSD1306_ALIGN_LEFT;
using esp_ssd1306::SSD1306_ALIGN_CENTER;
using esp_ssd1306::SSD1306_ALIGN_RIGHT;
