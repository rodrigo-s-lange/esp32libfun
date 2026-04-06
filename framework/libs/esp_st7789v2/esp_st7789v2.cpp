#include "esp_st7789v2.hpp"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp32libfun_delay.hpp"
#include "esp32libfun_gpio.hpp"

namespace esp_st7789v2 {

class DisplayLockGuard {
public:
    explicit DisplayLockGuard(const St7789v2 &display)
        : display_(display), locked_(display_.lock())
    {
    }

    ~DisplayLockGuard(void)
    {
        if (locked_) {
            display_.unlock();
        }
    }

    [[nodiscard]] bool locked(void) const
    {
        return locked_;
    }

private:
    const St7789v2 &display_;
    bool locked_ = false;
};

namespace {

static const char *TAG = "ESP_ST7789V2";

constexpr uint8_t kCmdSwReset = 0x01;
constexpr uint8_t kCmdSlpOut = 0x11;
constexpr uint8_t kCmdNorOn = 0x13;
constexpr uint8_t kCmdInvOff = 0x20;
constexpr uint8_t kCmdInvOn = 0x21;
constexpr uint8_t kCmdDispOff = 0x28;
constexpr uint8_t kCmdDispOn = 0x29;
constexpr uint8_t kCmdCaseT = 0x2A;
constexpr uint8_t kCmdRaseT = 0x2B;
constexpr uint8_t kCmdRamWr = 0x2C;
constexpr uint8_t kCmdMadCtl = 0x36;
constexpr uint8_t kCmdColMod = 0x3A;

constexpr uint8_t kColorMode16Bit = 0x55;
constexpr uint8_t kMadCtlRotation0 = 0xA0;
constexpr uint8_t kMadCtlRotation90 = 0x00;
constexpr uint8_t kMadCtlRotation180 = 0x60;
constexpr uint8_t kMadCtlRotation270 = 0xC0;

constexpr uint32_t kResetDelayMs = 150;
constexpr uint32_t kSleepOutDelayMs = 100;

int sevenSegGlyphWidth(int height, int thickness)
{
    const int width_from_height = (height * 3) / 5;
    const int width_from_thickness = thickness * 3;
    return (width_from_height > width_from_thickness) ? width_from_height : width_from_thickness;
}

const uint8_t *font5x7ForChar(char c)
{
    static const uint8_t glyph_space[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t glyph_qmark[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
    static const uint8_t glyph_dash[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
    static const uint8_t glyph_dot[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
    static const uint8_t glyph_colon[7] = {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00};
    static const uint8_t glyph_slash[7] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00};
    static const uint8_t glyph_plus[7] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
    static const uint8_t glyph_percent[7] = {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13};

    static const uint8_t digits[10][7] = {
        {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
        {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
        {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
        {0x1E, 0x01, 0x01, 0x06, 0x01, 0x01, 0x1E},
        {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
        {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
        {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
        {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x1C},
    };

    static const uint8_t letters[26][7] = {
        {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
        {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
        {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
        {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C},
        {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
        {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
        {0x0E, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0E},
        {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
        {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
        {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E},
        {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
        {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
        {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
        {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11},
        {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
        {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
        {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
        {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
        {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
        {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
        {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A},
        {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
        {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
    };

    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    }
    if (c >= '0' && c <= '9') {
        return digits[c - '0'];
    }
    if (c >= 'A' && c <= 'Z') {
        return letters[c - 'A'];
    }

    switch (c) {
        case ' ': return glyph_space;
        case '-': return glyph_dash;
        case '.': return glyph_dot;
        case ':': return glyph_colon;
        case '/': return glyph_slash;
        case '+': return glyph_plus;
        case '%': return glyph_percent;
        default: return glyph_qmark;
    }
}

} // namespace

esp_err_t St7789v2::ensureMutex(void)
{
    if (mutex_ != nullptr) {
        return ESP_OK;
    }

    mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
    return (mutex_ != nullptr) ? ESP_OK : ESP_ERR_NO_MEM;
}

bool St7789v2::lock(void) const
{
    return mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE;
}

void St7789v2::unlock(void) const
{
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

esp_err_t St7789v2::ensureReadyLocked(void) const
{
    if (!configured_ || !spi.ready(port_) || !spi.has(cs_pin_, port_)) {
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

TickType_t St7789v2::msToTicks(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);
    if (ticks == 0) {
        ticks = 1;
    }
    return ticks;
}

void St7789v2::resetStateLocked(void)
{
    configured_ = false;
    invert_ = true;
    backlight_on_ = false;
    cs_pin_ = -1;
    dc_pin_ = -1;
    rst_pin_ = -1;
    blk_pin_ = -1;
    port_ = DEFAULT_SPI_PORT;
    clock_hz_ = DEFAULT_CLOCK_HZ;
    width_ = DEFAULT_WIDTH;
    height_ = DEFAULT_HEIGHT;
    x_gap_ = DEFAULT_X_GAP;
    y_gap_ = DEFAULT_Y_GAP;
    rotation_ = ST7789V2_ROTATION_0;
    region_active_ = false;
    region_pixels_remaining_ = 0;
}

esp_err_t St7789v2::sendDataLocked(const uint8_t *data, size_t len)
{
    if (data == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpio.high(dc_pin_);
    if (err != ESP_OK) {
        return err;
    }

    return spi.write(cs_pin_, data, len, port_);
}

esp_err_t St7789v2::sendCommandLocked(uint8_t cmd, const uint8_t *data, size_t len)
{
    esp_err_t err = gpio.low(dc_pin_);
    if (err != ESP_OK) {
        return err;
    }

    err = spi.cmd(cs_pin_, cmd, port_);
    if (err != ESP_OK || len == 0) {
        return err;
    }

    return sendDataLocked(data, len);
}

esp_err_t St7789v2::resetLocked(void)
{
    if (rst_pin_ >= 0) {
        ESP_RETURN_ON_ERROR(gpio.high(rst_pin_), TAG, "rst high failed");
        vTaskDelay(msToTicks(10));
        ESP_RETURN_ON_ERROR(gpio.low(rst_pin_), TAG, "rst low failed");
        vTaskDelay(msToTicks(10));
        ESP_RETURN_ON_ERROR(gpio.high(rst_pin_), TAG, "rst high failed");
        vTaskDelay(msToTicks(120));
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(sendCommandLocked(kCmdSwReset), TAG, "software reset failed");
    vTaskDelay(msToTicks(20));
    return ESP_OK;
}

esp_err_t St7789v2::applyRotationLocked(St7789v2Rotation rotation)
{
    uint8_t madctl = kMadCtlRotation0;
    uint16_t width = DEFAULT_WIDTH;
    uint16_t height = DEFAULT_HEIGHT;
    uint16_t x_gap = DEFAULT_X_GAP;
    uint16_t y_gap = DEFAULT_Y_GAP;

    switch (rotation) {
        case ST7789V2_ROTATION_0:
            madctl = kMadCtlRotation0;
            width = DEFAULT_WIDTH;
            height = DEFAULT_HEIGHT;
            x_gap = DEFAULT_X_GAP;
            y_gap = DEFAULT_Y_GAP;
            break;
        case ST7789V2_ROTATION_90:
            madctl = kMadCtlRotation90;
            width = DEFAULT_HEIGHT;
            height = DEFAULT_WIDTH;
            x_gap = DEFAULT_Y_GAP;
            y_gap = DEFAULT_X_GAP;
            break;
        case ST7789V2_ROTATION_180:
            madctl = kMadCtlRotation180;
            width = DEFAULT_WIDTH;
            height = DEFAULT_HEIGHT;
            x_gap = DEFAULT_X_GAP;
            y_gap = DEFAULT_Y_GAP;
            break;
        case ST7789V2_ROTATION_270:
            madctl = kMadCtlRotation270;
            width = DEFAULT_HEIGHT;
            height = DEFAULT_WIDTH;
            x_gap = DEFAULT_Y_GAP;
            y_gap = DEFAULT_X_GAP;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(sendCommandLocked(kCmdMadCtl, &madctl, 1), TAG, "MADCTL write failed");

    rotation_ = rotation;
    width_ = width;
    height_ = height;
    x_gap_ = x_gap;
    y_gap_ = y_gap;
    return ESP_OK;
}

esp_err_t St7789v2::initSequenceLocked(void)
{
    const uint8_t caset[] = {0x00, 0x00, 0x00, 0xF0};
    const uint8_t raset[] = {0x00, 0x00, 0x01, 0x40};

    ESP_RETURN_ON_ERROR(resetLocked(), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(sendCommandLocked(kCmdSlpOut), TAG, "SLPOUT failed");
    vTaskDelay(msToTicks(kSleepOutDelayMs));
    ESP_RETURN_ON_ERROR(applyRotationLocked(ST7789V2_ROTATION_0), TAG, "rotation apply failed");
    ESP_RETURN_ON_ERROR(sendCommandLocked(kCmdColMod, &kColorMode16Bit, 1), TAG, "COLMOD failed");

    ESP_RETURN_ON_ERROR(sendCommandLocked(kCmdSwReset), TAG, "SWRESET failed");
    vTaskDelay(msToTicks(kResetDelayMs));
    ESP_RETURN_ON_ERROR(sendCommandLocked(kCmdSlpOut), TAG, "second SLPOUT failed");
    vTaskDelay(msToTicks(10));
    ESP_RETURN_ON_ERROR(sendCommandLocked(kCmdCaseT, caset, sizeof(caset)), TAG, "CASET failed");
    ESP_RETURN_ON_ERROR(sendCommandLocked(kCmdRaseT, raset, sizeof(raset)), TAG, "RASET failed");
    ESP_RETURN_ON_ERROR(sendCommandLocked(kCmdInvOn), TAG, "INVON failed");
    vTaskDelay(msToTicks(10));
    ESP_RETURN_ON_ERROR(sendCommandLocked(kCmdNorOn), TAG, "NORON failed");
    vTaskDelay(msToTicks(10));
    ESP_RETURN_ON_ERROR(sendCommandLocked(kCmdDispOn), TAG, "DISPON failed");
    vTaskDelay(msToTicks(10));

    invert_ = true;
    return ESP_OK;
}

esp_err_t St7789v2::setWindowLocked(int x, int y, int width, int height)
{
    if (x < 0 || y < 0 || width <= 0 || height <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((x + width) > width_ || (y + height) > height_) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t x0 = static_cast<uint16_t>(x + x_gap_);
    const uint16_t x1 = static_cast<uint16_t>(x0 + width - 1);
    const uint16_t y0 = static_cast<uint16_t>(y + y_gap_);
    const uint16_t y1 = static_cast<uint16_t>(y0 + height - 1);

    const uint8_t col[] = {
        static_cast<uint8_t>(x0 >> 8),
        static_cast<uint8_t>(x0 & 0xFF),
        static_cast<uint8_t>(x1 >> 8),
        static_cast<uint8_t>(x1 & 0xFF),
    };
    const uint8_t row[] = {
        static_cast<uint8_t>(y0 >> 8),
        static_cast<uint8_t>(y0 & 0xFF),
        static_cast<uint8_t>(y1 >> 8),
        static_cast<uint8_t>(y1 & 0xFF),
    };

    region_active_ = false;
    region_pixels_remaining_ = 0;

    ESP_RETURN_ON_ERROR(sendCommandLocked(kCmdCaseT, col, sizeof(col)), TAG, "CASET failed");
    return sendCommandLocked(kCmdRaseT, row, sizeof(row));
}

esp_err_t St7789v2::beginRegionLocked(int x, int y, int width, int height)
{
    ESP_RETURN_ON_ERROR(ensureReadyLocked(), TAG, "panel not ready");
    ESP_RETURN_ON_ERROR(setWindowLocked(x, y, width, height), TAG, "region window failed");
    ESP_RETURN_ON_ERROR(sendCommandLocked(kCmdRamWr), TAG, "region RAMWR failed");
    region_active_ = true;
    region_pixels_remaining_ = static_cast<size_t>(width) * static_cast<size_t>(height);
    return ESP_OK;
}

esp_err_t St7789v2::pushPixelsLocked(const uint16_t *pixels, size_t pixel_count)
{
    if (pixels == nullptr || pixel_count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!region_active_ || pixel_count > region_pixels_remaining_) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t offset = 0;
    while (offset < pixel_count) {
        const size_t chunk_pixels = ((pixel_count - offset) > FILL_BUFFER_PIXELS) ? FILL_BUFFER_PIXELS : (pixel_count - offset);
        for (size_t i = 0; i < chunk_pixels; ++i) {
            const uint16_t color = pixels[offset + i];
            const uint8_t hi = static_cast<uint8_t>(color >> 8);
            const uint8_t lo = static_cast<uint8_t>(color & 0xFF);
            fill_buffer_[i] = static_cast<uint16_t>(hi | (static_cast<uint16_t>(lo) << 8));
        }
        ESP_RETURN_ON_ERROR(sendDataLocked(reinterpret_cast<const uint8_t *>(fill_buffer_), chunk_pixels * sizeof(uint16_t)), TAG, "region data write failed");
        offset += chunk_pixels;
    }

    region_pixels_remaining_ -= pixel_count;
    if (region_pixels_remaining_ == 0U) {
        region_active_ = false;
    }

    return ESP_OK;
}

esp_err_t St7789v2::pushColorLocked(uint16_t color, size_t pixel_count)
{
    if (pixel_count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!region_active_ || pixel_count > region_pixels_remaining_) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t hi = static_cast<uint8_t>(color >> 8);
    const uint8_t lo = static_cast<uint8_t>(color & 0xFF);
    for (size_t i = 0; i < FILL_BUFFER_PIXELS; ++i) {
        fill_buffer_[i] = static_cast<uint16_t>(hi | (static_cast<uint16_t>(lo) << 8));
    }

    size_t remaining = pixel_count;
    while (remaining > 0U) {
        const size_t chunk_pixels = (remaining > FILL_BUFFER_PIXELS) ? FILL_BUFFER_PIXELS : remaining;
        ESP_RETURN_ON_ERROR(sendDataLocked(reinterpret_cast<const uint8_t *>(fill_buffer_), chunk_pixels * sizeof(uint16_t)), TAG, "region color write failed");
        remaining -= chunk_pixels;
    }

    region_pixels_remaining_ -= pixel_count;
    if (region_pixels_remaining_ == 0U) {
        region_active_ = false;
    }

    return ESP_OK;
}

esp_err_t St7789v2::endRegionLocked(void)
{
    region_active_ = false;
    region_pixels_remaining_ = 0;
    return ESP_OK;
}

esp_err_t St7789v2::fillRectLocked(int x, int y, int width, int height, uint16_t color)
{
    const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    ESP_RETURN_ON_ERROR(beginRegionLocked(x, y, width, height), TAG, "fill region start failed");
    ESP_RETURN_ON_ERROR(pushColorLocked(color, pixels), TAG, "fill region write failed");
    return endRegionLocked();
}

esp_err_t St7789v2::drawPixelLocked(int x, int y, uint16_t color)
{
    return fillRectLocked(x, y, 1, 1, color);
}

esp_err_t St7789v2::drawHLineLocked(int x, int y, int width, uint16_t color)
{
    return fillRectLocked(x, y, width, 1, color);
}

esp_err_t St7789v2::drawVLineLocked(int x, int y, int height, uint16_t color)
{
    return fillRectLocked(x, y, 1, height, color);
}

esp_err_t St7789v2::drawLineLocked(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (true) {
        ESP_RETURN_ON_ERROR(drawPixelLocked(x0, y0, color), TAG, "line pixel failed");
        if (x0 == x1 && y0 == y1) {
            break;
        }

        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }

    return ESP_OK;
}

esp_err_t St7789v2::drawRectLocked(int x, int y, int width, int height, uint16_t color)
{
    if (width <= 0 || height <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(drawHLineLocked(x, y, width, color), TAG, "rect top failed");
    ESP_RETURN_ON_ERROR(drawHLineLocked(x, y + height - 1, width, color), TAG, "rect bottom failed");
    ESP_RETURN_ON_ERROR(drawVLineLocked(x, y, height, color), TAG, "rect left failed");
    return drawVLineLocked(x + width - 1, y, height, color);
}

esp_err_t St7789v2::drawRoundRectLocked(int x, int y, int width, int height, int radius, uint16_t color)
{
    if (width <= 0 || height <= 0 || radius < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int max_radius = ((width < height) ? width : height) / 2;
    if (radius > max_radius) {
        radius = max_radius;
    }
    if (radius == 0) {
        return drawRectLocked(x, y, width, height, color);
    }

    int inner_w = width - (2 * radius);
    int inner_h = height - (2 * radius);
    if (inner_w > 0) {
        ESP_RETURN_ON_ERROR(drawHLineLocked(x + radius, y, inner_w, color), TAG, "round rect failed");
        ESP_RETURN_ON_ERROR(drawHLineLocked(x + radius, y + height - 1, inner_w, color), TAG, "round rect failed");
    }
    if (inner_h > 0) {
        ESP_RETURN_ON_ERROR(drawVLineLocked(x, y + radius, inner_h, color), TAG, "round rect failed");
        ESP_RETURN_ON_ERROR(drawVLineLocked(x + width - 1, y + radius, inner_h, color), TAG, "round rect failed");
    }

    int dx = radius;
    int dy = 0;
    int err = 1 - dx;
    int cx1 = x + radius;
    int cy1 = y + radius;
    int cx2 = x + width - radius - 1;
    int cy2 = y + height - radius - 1;

    while (dx >= dy) {
        ESP_RETURN_ON_ERROR(drawPixelLocked(cx1 - dx, cy1 - dy, color), TAG, "round rect failed");
        ESP_RETURN_ON_ERROR(drawPixelLocked(cx1 - dy, cy1 - dx, color), TAG, "round rect failed");
        ESP_RETURN_ON_ERROR(drawPixelLocked(cx2 + dx, cy1 - dy, color), TAG, "round rect failed");
        ESP_RETURN_ON_ERROR(drawPixelLocked(cx2 + dy, cy1 - dx, color), TAG, "round rect failed");
        ESP_RETURN_ON_ERROR(drawPixelLocked(cx1 - dx, cy2 + dy, color), TAG, "round rect failed");
        ESP_RETURN_ON_ERROR(drawPixelLocked(cx1 - dy, cy2 + dx, color), TAG, "round rect failed");
        ESP_RETURN_ON_ERROR(drawPixelLocked(cx2 + dx, cy2 + dy, color), TAG, "round rect failed");
        ESP_RETURN_ON_ERROR(drawPixelLocked(cx2 + dy, cy2 + dx, color), TAG, "round rect failed");

        ++dy;
        if (err < 0) {
            err += (2 * dy) + 1;
        } else {
            --dx;
            err += 2 * (dy - dx) + 1;
        }
    }

    return ESP_OK;
}

esp_err_t St7789v2::drawGridLocked(int x, int y, int width, int height, int cols, int rows, uint16_t color)
{
    if (cols <= 0 || rows <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(drawRectLocked(x, y, width, height, color), TAG, "grid border failed");

    for (int col = 1; col < cols; ++col) {
        int gx = x + ((width * col) / cols);
        ESP_RETURN_ON_ERROR(drawVLineLocked(gx, y, height, color), TAG, "grid column failed");
    }

    for (int row = 1; row < rows; ++row) {
        int gy = y + ((height * row) / rows);
        ESP_RETURN_ON_ERROR(drawHLineLocked(x, gy, width, color), TAG, "grid row failed");
    }

    return ESP_OK;
}

esp_err_t St7789v2::drawCircleLocked(int cx, int cy, int radius, uint16_t color)
{
    if (radius < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int x = radius;
    int y = 0;
    int err = 1 - x;

    while (x >= y) {
        ESP_RETURN_ON_ERROR(drawPixelLocked(cx + x, cy + y, color), TAG, "circle pixel failed");
        ESP_RETURN_ON_ERROR(drawPixelLocked(cx + y, cy + x, color), TAG, "circle pixel failed");
        ESP_RETURN_ON_ERROR(drawPixelLocked(cx - y, cy + x, color), TAG, "circle pixel failed");
        ESP_RETURN_ON_ERROR(drawPixelLocked(cx - x, cy + y, color), TAG, "circle pixel failed");
        ESP_RETURN_ON_ERROR(drawPixelLocked(cx - x, cy - y, color), TAG, "circle pixel failed");
        ESP_RETURN_ON_ERROR(drawPixelLocked(cx - y, cy - x, color), TAG, "circle pixel failed");
        ESP_RETURN_ON_ERROR(drawPixelLocked(cx + y, cy - x, color), TAG, "circle pixel failed");
        ESP_RETURN_ON_ERROR(drawPixelLocked(cx + x, cy - y, color), TAG, "circle pixel failed");

        ++y;
        if (err < 0) {
            err += (2 * y) + 1;
        } else {
            --x;
            err += 2 * (y - x) + 1;
        }
    }

    return ESP_OK;
}

esp_err_t St7789v2::fillCircleLocked(int cx, int cy, int radius, uint16_t color)
{
    if (radius < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int x = radius;
    int y = 0;
    int err = 1 - x;

    while (x >= y) {
        ESP_RETURN_ON_ERROR(drawHLineLocked(cx - x, cy + y, (2 * x) + 1, color), TAG, "circle fill failed");
        ESP_RETURN_ON_ERROR(drawHLineLocked(cx - x, cy - y, (2 * x) + 1, color), TAG, "circle fill failed");
        ESP_RETURN_ON_ERROR(drawHLineLocked(cx - y, cy + x, (2 * y) + 1, color), TAG, "circle fill failed");
        ESP_RETURN_ON_ERROR(drawHLineLocked(cx - y, cy - x, (2 * y) + 1, color), TAG, "circle fill failed");

        ++y;
        if (err < 0) {
            err += (2 * y) + 1;
        } else {
            --x;
            err += 2 * (y - x) + 1;
        }
    }

    return ESP_OK;
}

esp_err_t St7789v2::drawTriangleLocked(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    ESP_RETURN_ON_ERROR(drawLineLocked(x0, y0, x1, y1, color), TAG, "triangle edge failed");
    ESP_RETURN_ON_ERROR(drawLineLocked(x1, y1, x2, y2, color), TAG, "triangle edge failed");
    return drawLineLocked(x2, y2, x0, y0, color);
}

esp_err_t St7789v2::fillTriangleLocked(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    if (y0 > y1) {
        int tx = x0; int ty = y0; x0 = x1; y0 = y1; x1 = tx; y1 = ty;
    }
    if (y1 > y2) {
        int tx = x1; int ty = y1; x1 = x2; y1 = y2; x2 = tx; y2 = ty;
    }
    if (y0 > y1) {
        int tx = x0; int ty = y0; x0 = x1; y0 = y1; x1 = tx; y1 = ty;
    }

    if (y0 == y2) {
        int min_x = x0;
        int max_x = x0;
        if (x1 < min_x) min_x = x1;
        if (x2 < min_x) min_x = x2;
        if (x1 > max_x) max_x = x1;
        if (x2 > max_x) max_x = x2;
        return drawHLineLocked(min_x, y0, (max_x - min_x) + 1, color);
    }

    for (int y = y0; y <= y2; ++y) {
        int xa = x0;
        int xb = x0;

        if (y2 != y0) {
            xa = x0 + ((x2 - x0) * (y - y0)) / (y2 - y0);
        }

        if (y < y1) {
            xb = (y1 != y0) ? (x0 + ((x1 - x0) * (y - y0)) / (y1 - y0)) : x1;
        } else {
            xb = (y2 != y1) ? (x1 + ((x2 - x1) * (y - y1)) / (y2 - y1)) : x2;
        }

        if (xa > xb) {
            int t = xa;
            xa = xb;
            xb = t;
        }

        ESP_RETURN_ON_ERROR(drawHLineLocked(xa, y, (xb - xa) + 1, color), TAG, "fill triangle failed");
    }

    return ESP_OK;
}

esp_err_t St7789v2::fillRoundRectLocked(int x, int y, int width, int height, int radius, uint16_t color)
{
    if (width <= 0 || height <= 0 || radius < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int max_radius = ((width < height) ? width : height) / 2;
    if (radius > max_radius) {
        radius = max_radius;
    }
    if (radius == 0) {
        return fillRectLocked(x, y, width, height, color);
    }

    int inner_w = width - (2 * radius);
    int inner_h = height - (2 * radius);
    if (inner_w > 0) {
        ESP_RETURN_ON_ERROR(fillRectLocked(x + radius, y, inner_w, height, color), TAG, "fill round rect failed");
    }
    if (inner_h > 0) {
        ESP_RETURN_ON_ERROR(fillRectLocked(x, y + radius, radius, inner_h, color), TAG, "fill round rect failed");
        ESP_RETURN_ON_ERROR(fillRectLocked(x + width - radius, y + radius, radius, inner_h, color), TAG, "fill round rect failed");
    }

    int dx = radius;
    int dy = 0;
    int err = 1 - dx;

    while (dx >= dy) {
        int top_y = y + radius - dx;
        int inner_w1 = width - (2 * (radius - dy));
        int inner_w2 = width - (2 * (radius - dx));

        if (inner_w1 > 0) {
            ESP_RETURN_ON_ERROR(drawHLineLocked(x + radius - dy, top_y, inner_w1, color), TAG, "fill round rect failed");
            ESP_RETURN_ON_ERROR(drawHLineLocked(x + radius - dy, y + height - radius - 1 + dx, inner_w1, color), TAG, "fill round rect failed");
        }
        if (inner_w2 > 0) {
            ESP_RETURN_ON_ERROR(drawHLineLocked(x + radius - dx, y + radius - dy, inner_w2, color), TAG, "fill round rect failed");
            ESP_RETURN_ON_ERROR(drawHLineLocked(x + radius - dx, y + height - radius - 1 + dy, inner_w2, color), TAG, "fill round rect failed");
        }

        ++dy;
        if (err < 0) {
            err += (2 * dy) + 1;
        } else {
            --dx;
            err += 2 * (dy - dx) + 1;
        }
    }

    return ESP_OK;
}

esp_err_t St7789v2::drawCharLocked(int x, int y, char c, uint16_t fg, uint16_t bg, int scale)
{
    char text[2] = {c, '\0'};
    return drawTextLocked(x, y, text, fg, bg, scale);
}

esp_err_t St7789v2::drawTextLocked(int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale)
{
    if (text == nullptr || scale <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t len = strlen(text);
    if (len == 0U) {
        return ESP_OK;
    }
    if (x < 0 || y < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int total_width = textWidth(text, scale);
    if ((x + total_width) > width_ || (y + (7 * scale)) > height_) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t row_buffer[MAX_FILL_WIDTH] = {};
    if (static_cast<size_t>(total_width) > MAX_FILL_WIDTH) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = beginRegionLocked(x, y, total_width, 7 * scale);
    if (err != ESP_OK) {
        return err;
    }

    for (int row = 0; row < 7; ++row) {
        for (int sy = 0; sy < scale; ++sy) {
            int cursor = 0;
            for (size_t i = 0; i < len; ++i) {
                const uint8_t *glyph = font5x7ForChar(text[i]);
                for (int col = 0; col < 5; ++col) {
                    const uint16_t color = (glyph[row] & (1U << (4 - col))) != 0U ? fg : bg;
                    for (int sx = 0; sx < scale; ++sx) {
                        row_buffer[cursor++] = color;
                    }
                }
                if (i + 1U < len) {
                    for (int sx = 0; sx < scale; ++sx) {
                        row_buffer[cursor++] = bg;
                    }
                }
            }

            err = pushPixelsLocked(row_buffer, static_cast<size_t>(total_width));
            if (err != ESP_OK) {
                endRegionLocked();
                return err;
            }
        }
    }

    return endRegionLocked();
}

esp_err_t St7789v2::drawTextBoxLocked(int x, int y, int width, int height, const char *text, uint16_t fg, uint16_t bg, int scale, St7789v2Align align)
{
    if (text == nullptr || width <= 0 || height <= 0 || scale <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int text_width = textWidth(text, scale);
    const int text_height = 7 * scale;
    if (text_width <= 0 || text_width > width || text_height > height) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(fillRectLocked(x, y, width, height, bg), TAG, "text box clear failed");

    int draw_x = x;
    switch (align) {
        case ST7789V2_ALIGN_LEFT:
            break;
        case ST7789V2_ALIGN_CENTER:
            draw_x = x + ((width - text_width) / 2);
            break;
        case ST7789V2_ALIGN_RIGHT:
            draw_x = x + (width - text_width);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    const int draw_y = y + ((height - text_height) / 2);
    return drawTextLocked(draw_x, draw_y, text, fg, bg, scale);
}

esp_err_t St7789v2::draw7SegCharLocked(int x, int y, char c, int height, int thickness, uint16_t fg, uint16_t bg)
{
    if (height <= 0 || thickness <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int width = sevenSegGlyphWidth(height, thickness);
    const int radius = thickness / 2;
    const int pad = thickness / 3;
    const int hseg_x = x + (thickness / 2);
    const int hseg_w = width - thickness;
    const int vseg_h = (height - (3 * thickness)) / 2;
    const int left_x = x;
    const int right_x = x + width - thickness;
    const int top_y = y;
    const int mid_y = y + thickness + vseg_h;
    const int upper_y = y + thickness;
    const int lower_y = y + (2 * thickness) + vseg_h;
    const int bottom_y = y + height - thickness;

    if (hseg_w <= 0 || vseg_h <= 0 || width <= (2 * thickness)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(fillRectLocked(x, y, width, height, bg), TAG, "7seg clear failed");

    if (c == ' ') {
        return ESP_OK;
    }

    if (c == 'o' || c == 'O' || c == '*') {
        int seg_t = (thickness * 2) / 3;
        if (seg_t < 3) {
            seg_t = 3;
        }

        int mini_w = (width * 2) / 3;
        int mini_h = height / 2;
        if (mini_w < (seg_t * 4)) {
            mini_w = seg_t * 4;
        }
        if (mini_h < (seg_t * 4)) {
            mini_h = seg_t * 4;
        }

        int mini_x = x + width - mini_w - 1;
        int mini_y = y;
        int mini_v = (mini_h - (2 * seg_t)) / 2;
        int mini_inner_w = mini_w - seg_t;
        int mini_r = seg_t / 2;

        if (mini_x < x) {
            mini_x = x;
        }
        if (mini_v <= 0 || mini_inner_w <= 0) {
            return ESP_ERR_INVALID_ARG;
        }

        ESP_RETURN_ON_ERROR(fillRoundRectLocked(mini_x + (seg_t / 2), mini_y, mini_inner_w, seg_t, mini_r, fg), TAG, "7seg degree failed");
        ESP_RETURN_ON_ERROR(fillRoundRectLocked(mini_x, mini_y + seg_t, seg_t, mini_v, mini_r, fg), TAG, "7seg degree failed");
        ESP_RETURN_ON_ERROR(fillRoundRectLocked(mini_x + mini_w - seg_t, mini_y + seg_t, seg_t, mini_v, mini_r, fg), TAG, "7seg degree failed");
        return fillRoundRectLocked(mini_x + (seg_t / 2), mini_y + seg_t + mini_v, mini_inner_w, seg_t, mini_r, fg);
    }

    if (c == '.') {
        return fillCircleLocked(x + width - radius - 1, y + height - radius - 1, (radius > 0) ? radius : 1, fg);
    }

    if (c == ':') {
        const int dot_r = (radius > 0) ? radius : 1;
        const int dot_x = x + (width / 2);
        ESP_RETURN_ON_ERROR(fillCircleLocked(dot_x, y + (height / 3), dot_r, fg), TAG, "7seg colon failed");
        return fillCircleLocked(dot_x, y + ((height * 2) / 3), dot_r, fg);
    }

    if (c == ',') {
        const int dot_r = (radius > 0) ? radius : 1;
        ESP_RETURN_ON_ERROR(fillCircleLocked(x + width - dot_r - 1, y + height - dot_r - 1, dot_r, fg), TAG, "7seg comma failed");
        return drawLineLocked(x + width - dot_r - 1, y + height - 1, x + width - thickness - pad, y + height + thickness - 1, fg);
    }

    uint8_t mask = 0;
    switch (c) {
        case '0': mask = 0x3F; break;
        case '1': mask = 0x06; break;
        case '2': mask = 0x5B; break;
        case '3': mask = 0x4F; break;
        case '4': mask = 0x66; break;
        case '5': mask = 0x6D; break;
        case '6': mask = 0x7D; break;
        case '7': mask = 0x07; break;
        case '8': mask = 0x7F; break;
        case '9': mask = 0x6F; break;
        case 'A':
        case 'a': mask = 0x77; break;
        case 'B':
        case 'b': mask = 0x7C; break;
        case 'C':
        case 'c': mask = 0x39; break;
        case 'D':
        case 'd': mask = 0x5E; break;
        case 'E':
        case 'e': mask = 0x79; break;
        case 'F':
        case 'f': mask = 0x71; break;
        case 'G':
        case 'g': mask = 0x6F; break;
        case 'H':
        case 'h': mask = 0x76; break;
        case 'I':
        case 'i': mask = 0x06; break;
        case 'J':
        case 'j': mask = 0x1E; break;
        case 'L':
        case 'l': mask = 0x38; break;
        case 'N':
        case 'n': mask = 0x54; break;
        case 'P':
        case 'p': mask = 0x73; break;
        case 'R':
        case 'r': mask = 0x50; break;
        case 'S':
        case 's': mask = 0x6D; break;
        case 'T':
        case 't': mask = 0x78; break;
        case 'U':
        case 'u': mask = 0x3E; break;
        case 'Y':
        case 'y': mask = 0x6E; break;
        default:
            return ESP_OK;
    }

    if (mask & 0x01) ESP_RETURN_ON_ERROR(fillRoundRectLocked(hseg_x, top_y, hseg_w, thickness, radius, fg), TAG, "7seg failed");
    if (mask & 0x02) ESP_RETURN_ON_ERROR(fillRoundRectLocked(right_x, upper_y, thickness, vseg_h, radius, fg), TAG, "7seg failed");
    if (mask & 0x04) ESP_RETURN_ON_ERROR(fillRoundRectLocked(right_x, lower_y, thickness, vseg_h, radius, fg), TAG, "7seg failed");
    if (mask & 0x08) ESP_RETURN_ON_ERROR(fillRoundRectLocked(hseg_x, bottom_y, hseg_w, thickness, radius, fg), TAG, "7seg failed");
    if (mask & 0x10) ESP_RETURN_ON_ERROR(fillRoundRectLocked(left_x, lower_y, thickness, vseg_h, radius, fg), TAG, "7seg failed");
    if (mask & 0x20) ESP_RETURN_ON_ERROR(fillRoundRectLocked(left_x, upper_y, thickness, vseg_h, radius, fg), TAG, "7seg failed");
    if (mask & 0x40) ESP_RETURN_ON_ERROR(fillRoundRectLocked(hseg_x, mid_y, hseg_w, thickness, radius, fg), TAG, "7seg failed");

    return ESP_OK;
}

esp_err_t St7789v2::draw7SegTextLocked(int x, int y, const char *text, int height, int thickness, uint16_t fg, uint16_t bg)
{
    if (text == nullptr || height <= 0 || thickness <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int width = sevenSegGlyphWidth(height, thickness);
    const int spacing = thickness;
    int cursor_x = x;

    for (size_t i = 0; text[i] != '\0'; ++i) {
        char c = text[i];
        if (static_cast<unsigned char>(text[i]) == 0xC2 && static_cast<unsigned char>(text[i + 1]) == 0xB0) {
            c = 'O';
            ++i;
        }

        ESP_RETURN_ON_ERROR(draw7SegCharLocked(cursor_x, y, c, height, thickness, fg, bg), TAG, "7seg text failed");
        cursor_x += width + spacing;
    }

    return ESP_OK;
}

esp_err_t St7789v2::draw7SegBoxLocked(int x, int y, int width, int height, int thickness, const char *text, uint16_t fg, uint16_t bg, St7789v2Align align)
{
    if (text == nullptr || width <= 0 || height <= 0 || thickness <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int text_width = sevenSegTextWidth(text, height, thickness);
    if (text_width <= 0 || text_width > width) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(fillRectLocked(x, y, width, height, bg), TAG, "7seg box clear failed");

    int draw_x = x;
    switch (align) {
        case ST7789V2_ALIGN_LEFT:
            break;
        case ST7789V2_ALIGN_CENTER:
            draw_x = x + ((width - text_width) / 2);
            break;
        case ST7789V2_ALIGN_RIGHT:
            draw_x = x + (width - text_width);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    return draw7SegTextLocked(draw_x, y, text, height, thickness, fg, bg);
}

esp_err_t St7789v2::drawProgressBarLocked(int x,
                                          int y,
                                          int width,
                                          int height,
                                          int min,
                                          int max,
                                          int value,
                                          uint16_t border_color,
                                          uint16_t fill_color,
                                          uint16_t bg_color)
{
    if (width <= 2 || height <= 2 || max <= min) {
        return ESP_ERR_INVALID_ARG;
    }

    if (value < min) {
        value = min;
    }
    if (value > max) {
        value = max;
    }

    const int inner_x = x + 1;
    const int inner_y = y + 1;
    const int inner_w = width - 2;
    const int inner_h = height - 2;
    const int range = max - min;
    const int fill_w = (inner_w * (value - min)) / range;

    ESP_RETURN_ON_ERROR(drawRectLocked(x, y, width, height, border_color), TAG, "progress border failed");
    ESP_RETURN_ON_ERROR(fillRectLocked(inner_x, inner_y, inner_w, inner_h, bg_color), TAG, "progress bg failed");
    if (fill_w > 0) {
        ESP_RETURN_ON_ERROR(fillRectLocked(inner_x, inner_y, fill_w, inner_h, fill_color), TAG, "progress fill failed");
    }

    return ESP_OK;
}

esp_err_t St7789v2::init(int cs_pin, int dc_pin, int rst_pin, int blk_pin, int port, uint32_t clock_hz)
{
    if (dc_pin < 0 || clock_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!spi.ready(port)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (configured_) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ensureMutex();
    if (err != ESP_OK) {
        return err;
    }

    err = gpio.cfg(dc_pin, OUTPUT);
    if (err != ESP_OK) {
        return err;
    }
    if (rst_pin >= 0) {
        err = gpio.cfg(rst_pin, OUTPUT);
        if (err != ESP_OK) {
            return err;
        }
    }
    if (blk_pin >= 0) {
        err = gpio.cfg(blk_pin, OUTPUT);
        if (err != ESP_OK) {
            return err;
        }
    }

    err = spi.add(cs_pin, clock_hz, SPI_MODE_0, port);
    if (err != ESP_OK) {
        return err;
    }

    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        spi.remove(cs_pin, port);
        return ESP_ERR_INVALID_STATE;
    }

    cs_pin_ = cs_pin;
    dc_pin_ = dc_pin;
    rst_pin_ = rst_pin;
    blk_pin_ = blk_pin;
    port_ = port;
    clock_hz_ = clock_hz;

    if (blk_pin_ >= 0) {
        err = gpio.low(blk_pin_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "backlight off failed: %s", esp_err_to_name(err));
            goto fail;
        }
    }

    err = initSequenceLocked();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel init sequence failed: %s", esp_err_to_name(err));
        goto fail;
    }

    configured_ = true;
    if (blk_pin_ >= 0) {
        err = gpio.high(blk_pin_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "backlight on failed: %s", esp_err_to_name(err));
            goto fail;
        }
        backlight_on_ = true;
    }

    ESP_LOGI(TAG, "initialized on SPI bus %d CS=%d DC=%d RST=%d BLK=%d size=%ux%u gap=%u,%u",
             port_,
             cs_pin_,
             dc_pin_,
             rst_pin_,
             blk_pin_,
             width_,
             height_,
             x_gap_,
             y_gap_);
    return ESP_OK;

fail:
    spi.remove(cs_pin, port);
    resetStateLocked();
    return err;
}

esp_err_t St7789v2::end(void)
{
    if (mutex_ == nullptr) {
        return ESP_OK;
    }

    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!configured_) {
        return ESP_OK;
    }

    (void)sendCommandLocked(kCmdDispOff);
    if (blk_pin_ >= 0) {
        (void)gpio.low(blk_pin_);
    }

    const int cs_pin = cs_pin_;
    const int port = port_;
    const esp_err_t err = spi.remove(cs_pin, port);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        return err;
    }

    resetStateLocked();
    return ESP_OK;
}

bool St7789v2::ready(void) const
{
    if (mutex_ == nullptr) {
        return false;
    }

    DisplayLockGuard guard(*this);
    return guard.locked() && configured_;
}

esp_err_t St7789v2::setRotation(St7789v2Rotation rotation)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(ensureReadyLocked(), TAG, "panel not ready");
    return applyRotationLocked(rotation);
}

esp_err_t St7789v2::setInvert(bool invert)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(ensureReadyLocked(), TAG, "panel not ready");
    ESP_RETURN_ON_ERROR(sendCommandLocked(invert ? kCmdInvOn : kCmdInvOff), TAG, "invert command failed");
    invert_ = invert;
    return ESP_OK;
}

esp_err_t St7789v2::backlight(bool on)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(ensureReadyLocked(), TAG, "panel not ready");
    if (blk_pin_ < 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_RETURN_ON_ERROR(gpio.write(blk_pin_, on), TAG, "backlight write failed");
    backlight_on_ = on;
    return ESP_OK;
}

esp_err_t St7789v2::fillScreen(uint16_t color)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return fillRectLocked(0, 0, width_, height_, color);
}

esp_err_t St7789v2::fillRect(int x, int y, int width, int height, uint16_t color)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return fillRectLocked(x, y, width, height, color);
}

esp_err_t St7789v2::drawPixel(int x, int y, uint16_t color)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return drawPixelLocked(x, y, color);
}

esp_err_t St7789v2::drawLine(int x0, int y0, int x1, int y1, uint16_t color)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return drawLineLocked(x0, y0, x1, y1, color);
}

esp_err_t St7789v2::drawHLine(int x, int y, int width, uint16_t color)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return drawHLineLocked(x, y, width, color);
}

esp_err_t St7789v2::drawVLine(int x, int y, int height, uint16_t color)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return drawVLineLocked(x, y, height, color);
}

esp_err_t St7789v2::drawRect(int x, int y, int width, int height, uint16_t color)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return drawRectLocked(x, y, width, height, color);
}

esp_err_t St7789v2::drawRoundRect(int x, int y, int width, int height, int radius, uint16_t color)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return drawRoundRectLocked(x, y, width, height, radius, color);
}

esp_err_t St7789v2::drawGrid(int x, int y, int width, int height, int cols, int rows, uint16_t color)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return drawGridLocked(x, y, width, height, cols, rows, color);
}

esp_err_t St7789v2::drawCircle(int cx, int cy, int radius, uint16_t color)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return drawCircleLocked(cx, cy, radius, color);
}

esp_err_t St7789v2::fillCircle(int cx, int cy, int radius, uint16_t color)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return fillCircleLocked(cx, cy, radius, color);
}

esp_err_t St7789v2::drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return drawTriangleLocked(x0, y0, x1, y1, x2, y2, color);
}

esp_err_t St7789v2::fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return fillTriangleLocked(x0, y0, x1, y1, x2, y2, color);
}

esp_err_t St7789v2::fillRoundRect(int x, int y, int width, int height, int radius, uint16_t color)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return fillRoundRectLocked(x, y, width, height, radius, color);
}

esp_err_t St7789v2::beginRegion(int x, int y, int width, int height)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return beginRegionLocked(x, y, width, height);
}

esp_err_t St7789v2::pushPixels(const uint16_t *pixels, size_t pixel_count)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return pushPixelsLocked(pixels, pixel_count);
}

esp_err_t St7789v2::pushColor(uint16_t color, size_t pixel_count)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return pushColorLocked(color, pixel_count);
}

esp_err_t St7789v2::endRegion(void)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return endRegionLocked();
}

esp_err_t St7789v2::writeRegion(int x, int y, int width, int height, const uint16_t *pixels, size_t pixel_count)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    const size_t expected_pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (pixel_count != expected_pixels) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(beginRegionLocked(x, y, width, height), TAG, "write region start failed");
    ESP_RETURN_ON_ERROR(pushPixelsLocked(pixels, pixel_count), TAG, "write region data failed");
    return endRegionLocked();
}

esp_err_t St7789v2::fillRegion(int x, int y, int width, int height, uint16_t color)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    ESP_RETURN_ON_ERROR(beginRegionLocked(x, y, width, height), TAG, "fill region start failed");
    ESP_RETURN_ON_ERROR(pushColorLocked(color, pixel_count), TAG, "fill region data failed");
    return endRegionLocked();
}

esp_err_t St7789v2::drawChar(int x, int y, char c, uint16_t fg, uint16_t bg, int scale)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return drawCharLocked(x, y, c, fg, bg, scale);
}

esp_err_t St7789v2::drawText(int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return drawTextLocked(x, y, text, fg, bg, scale);
}

esp_err_t St7789v2::drawTextAligned(int y, const char *text, uint16_t fg, uint16_t bg, int scale, St7789v2Align align)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (text == nullptr || scale <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int width = textWidth(text, scale);
    int x = 0;
    switch (align) {
        case ST7789V2_ALIGN_LEFT:
            x = 0;
            break;
        case ST7789V2_ALIGN_CENTER:
            x = static_cast<int>(width_ - width) / 2;
            break;
        case ST7789V2_ALIGN_RIGHT:
            x = static_cast<int>(width_ - width);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    return drawTextLocked(x, y, text, fg, bg, scale);
}

esp_err_t St7789v2::drawTextBox(int x, int y, int width, int height, const char *text, uint16_t fg, uint16_t bg, int scale, St7789v2Align align)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return drawTextBoxLocked(x, y, width, height, text, fg, bg, scale, align);
}

esp_err_t St7789v2::draw7SegChar(int x, int y, char c, int height, int thickness, uint16_t fg, uint16_t bg)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return draw7SegCharLocked(x, y, c, height, thickness, fg, bg);
}

esp_err_t St7789v2::draw7SegText(int x, int y, const char *text, int height, int thickness, uint16_t fg, uint16_t bg)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return draw7SegTextLocked(x, y, text, height, thickness, fg, bg);
}

esp_err_t St7789v2::draw7SegBox(int x, int y, int width, int height, int thickness, const char *text, uint16_t fg, uint16_t bg, St7789v2Align align)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return draw7SegBoxLocked(x, y, width, height, thickness, text, fg, bg, align);
}

esp_err_t St7789v2::update7SegBoxIfChanged(St7789v2SevenSegBoxState *box, const char *text)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (box == nullptr || text == nullptr || !box->initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strncmp(box->last_text, text, sizeof(box->last_text)) == 0) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(draw7SegBoxLocked(box->x, box->y, box->width, box->height, box->thickness, text, box->fg, box->bg, box->align), TAG, "7seg box update failed");
    strncpy(box->last_text, text, sizeof(box->last_text) - 1U);
    box->last_text[sizeof(box->last_text) - 1U] = '\0';
    return ESP_OK;
}

esp_err_t St7789v2::updateTextBoxIfChanged(St7789v2TextBoxState *box, const char *text)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (box == nullptr || text == nullptr || !box->initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strncmp(box->last_text, text, sizeof(box->last_text)) == 0) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(drawTextBoxLocked(box->x, box->y, box->width, box->height, text, box->fg, box->bg, box->scale, box->align), TAG, "text box update failed");
    strncpy(box->last_text, text, sizeof(box->last_text) - 1U);
    box->last_text[sizeof(box->last_text) - 1U] = '\0';
    return ESP_OK;
}

esp_err_t St7789v2::drawProgressBar(int x,
                                    int y,
                                    int width,
                                    int height,
                                    int min,
                                    int max,
                                    int value,
                                    uint16_t border_color,
                                    uint16_t fill_color,
                                    uint16_t bg_color)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    return drawProgressBarLocked(x, y, width, height, min, max, value, border_color, fill_color, bg_color);
}

esp_err_t St7789v2::updateProgressBarIfChanged(St7789v2ProgressBarState *bar, int value)
{
    DisplayLockGuard guard(*this);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (bar == nullptr || !bar->initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    if (bar->value == value) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(drawProgressBarLocked(bar->x,
                                              bar->y,
                                              bar->width,
                                              bar->height,
                                              bar->min,
                                              bar->max,
                                              value,
                                              bar->border_color,
                                              bar->fill_color,
                                              bar->bg_color),
                        TAG,
                        "progress update failed");
    bar->value = value;
    return ESP_OK;
}

int St7789v2::textWidth(const char *text, int scale) const
{
    if (text == nullptr || scale <= 0) {
        return 0;
    }

    const size_t len = strlen(text);
    if (len == 0U) {
        return 0;
    }

    return static_cast<int>(((len * 6U) - 1U) * static_cast<size_t>(scale));
}

int St7789v2::sevenSegTextWidth(const char *text, int height, int thickness) const
{
    if (text == nullptr || height <= 0 || thickness <= 0) {
        return 0;
    }

    const int width = sevenSegGlyphWidth(height, thickness);
    const int spacing = thickness;
    int glyphs = 0;
    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (static_cast<unsigned char>(text[i]) == 0xC2 && static_cast<unsigned char>(text[i + 1]) == 0xB0) {
            ++i;
        }
        ++glyphs;
    }

    if (glyphs == 0) {
        return 0;
    }

    return (glyphs * (width + spacing)) - spacing;
}

uint16_t St7789v2::width(void) const
{
    return width_;
}

uint16_t St7789v2::height(void) const
{
    return height_;
}

uint16_t St7789v2::xGap(void) const
{
    return x_gap_;
}

uint16_t St7789v2::yGap(void) const
{
    return y_gap_;
}

int St7789v2::csPin(void) const
{
    return cs_pin_;
}

int St7789v2::dcPin(void) const
{
    return dc_pin_;
}

int St7789v2::rstPin(void) const
{
    return rst_pin_;
}

int St7789v2::blkPin(void) const
{
    return blk_pin_;
}

int St7789v2::port(void) const
{
    return port_;
}

uint32_t St7789v2::clockHz(void) const
{
    return clock_hz_;
}

St7789v2Rotation St7789v2::rotation(void) const
{
    return rotation_;
}

bool St7789v2::inverted(void) const
{
    return invert_;
}

bool St7789v2::hasBacklight(void) const
{
    return blk_pin_ >= 0;
}

bool St7789v2::backlight(void) const
{
    return backlight_on_;
}

bool St7789v2::regionActive(void) const
{
    return region_active_;
}

size_t St7789v2::regionPixelsRemaining(void) const
{
    return region_pixels_remaining_;
}

St7789v2 st7789v2;

} // namespace esp_st7789v2
