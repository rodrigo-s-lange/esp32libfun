#include "esp_ssd1306.hpp"

#include <cstring>

#include "esp32libfun_i2c.hpp"
#include "esp_check.h"

namespace esp_ssd1306 {

namespace {

constexpr uint8_t kControlCommand = 0x00;
constexpr uint8_t kControlData = 0x40;
constexpr size_t kI2cChunkBytes = 16;

constexpr uint8_t kCmdDisplayOff = 0xAE;
constexpr uint8_t kCmdDisplayOn = 0xAF;
constexpr uint8_t kCmdContrast = 0x81;
constexpr uint8_t kCmdDisplayAllOnResume = 0xA4;
constexpr uint8_t kCmdNormalDisplay = 0xA6;
constexpr uint8_t kCmdInvertDisplay = 0xA7;
constexpr uint8_t kCmdMemoryMode = 0x20;
constexpr uint8_t kCmdColumnAddr = 0x21;
constexpr uint8_t kCmdPageAddr = 0x22;
constexpr uint8_t kCmdPageStart = 0xB0;
constexpr uint8_t kCmdComScanDec = 0xC8;
constexpr uint8_t kCmdSegRemap = 0xA1;
constexpr uint8_t kCmdChargePump = 0x8D;
constexpr uint8_t kCmdComPins = 0xDA;
constexpr uint8_t kCmdMuxRatio = 0xA8;
constexpr uint8_t kCmdDisplayOffset = 0xD3;
constexpr uint8_t kCmdDisplayClockDiv = 0xD5;
constexpr uint8_t kCmdPrecharge = 0xD9;
constexpr uint8_t kCmdVcomDetect = 0xDB;
constexpr uint8_t kCmdStartLine = 0x40;

} // namespace

bool Ssd1306::isValidAddress(uint16_t address)
{
    return address == DEFAULT_ADDRESS || address == ALTERNATE_ADDRESS;
}

bool Ssd1306::isSupportedGeometry(int width, int height)
{
    return width == DEFAULT_WIDTH && (height == 32 || height == DEFAULT_HEIGHT);
}

bool Ssd1306::copyText(char *dst, size_t dst_len, const char *src)
{
    if (dst == nullptr || dst_len == 0 || src == nullptr) {
        return false;
    }

    const size_t len = strlen(src);
    if (len >= dst_len) {
        return false;
    }

    memcpy(dst, src, len + 1);
    return true;
}

int Ssd1306::sevenSegGlyphWidth(int height, int thickness)
{
    const int width_from_height = (height * 3) / 5;
    const int width_from_thickness = thickness * 3;
    return (width_from_height > width_from_thickness) ? width_from_height : width_from_thickness;
}

const uint8_t *Ssd1306::font5x7ForChar(char c)
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
        c = static_cast<char>(c - 'a' + 'A');
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

esp_err_t Ssd1306::write(uint8_t control, const uint8_t *data, size_t len) const
{
    if (!configured_ || data == nullptr || len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t packet[kI2cChunkBytes + 1] = {};
    packet[0] = control;

    size_t offset = 0;
    while (offset < len) {
        size_t chunk = len - offset;
        if (chunk > kI2cChunkBytes) {
            chunk = kI2cChunkBytes;
        }

        memcpy(&packet[1], &data[offset], chunk);
        const esp_err_t err = i2c.write(address_, packet, chunk + 1, port_);
        if (err != ESP_OK) {
            return err;
        }

        offset += chunk;
    }

    return ESP_OK;
}

esp_err_t Ssd1306::command(const uint8_t *data, size_t len) const
{
    return write(kControlCommand, data, len);
}

esp_err_t Ssd1306::data(const uint8_t *data, size_t len) const
{
    return write(kControlData, data, len);
}

void Ssd1306::clearState(void)
{
    configured_ = false;
    address_ = DEFAULT_ADDRESS;
    port_ = 0;
    width_ = DEFAULT_WIDTH;
    height_ = DEFAULT_HEIGHT;
    pages_ = (DEFAULT_HEIGHT / 8);
    buffer_size_ = MAX_FRAME_BYTES;
    memset(buffer_, 0x00, sizeof(buffer_));
}

esp_err_t Ssd1306::init(uint16_t address, int port, int width, int height)
{
    if (configured_) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!isValidAddress(address) || !isSupportedGeometry(width, height) || !i2c.ready(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = i2c.probe(address, port);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c.add(address, port);
    if (err != ESP_OK) {
        return err;
    }

    configured_ = true;
    address_ = address;
    port_ = port;
    width_ = width;
    height_ = height;
    pages_ = (height / 8);
    buffer_size_ = static_cast<size_t>(width * height / 8);
    memset(buffer_, 0x00, sizeof(buffer_));

    const uint8_t init_seq[] = {
        kCmdDisplayOff,
        kCmdDisplayClockDiv, 0x80,
        kCmdMuxRatio, static_cast<uint8_t>(height - 1),
        kCmdDisplayOffset, 0x00,
        kCmdStartLine,
        kCmdChargePump, 0x14,
        kCmdMemoryMode, 0x00,
        kCmdSegRemap,
        kCmdComScanDec,
        kCmdComPins, static_cast<uint8_t>(height == 32 ? 0x02 : 0x12),
        kCmdContrast, static_cast<uint8_t>(height == 32 ? 0x8F : 0xCF),
        kCmdPrecharge, 0xF1,
        kCmdVcomDetect, 0x40,
        kCmdDisplayAllOnResume,
        kCmdNormalDisplay,
        kCmdDisplayOn,
    };

    err = command(init_seq, sizeof(init_seq));
    if (err != ESP_OK) {
        i2c.remove(address, port);
        clearState();
        return err;
    }

    return present();
}

esp_err_t Ssd1306::end(void)
{
    if (!configured_) {
        return ESP_OK;
    }

    const esp_err_t err = i2c.remove(address_, port_);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        return err;
    }

    clearState();
    return ESP_OK;
}

bool Ssd1306::ready(void) const
{
    return configured_;
}

esp_err_t Ssd1306::present(void)
{
    if (!configured_) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t setup[] = {
        kCmdColumnAddr, 0x00, static_cast<uint8_t>(width_ - 1),
        kCmdPageAddr, 0x00, static_cast<uint8_t>(pages_ - 1),
    };

    esp_err_t err = command(setup, sizeof(setup));
    if (err != ESP_OK) {
        return err;
    }

    for (int page = 0; page < pages_; ++page) {
        const uint8_t page_cmd[] = {
            static_cast<uint8_t>(kCmdPageStart + page),
            0x00,
            0x10,
        };

        err = command(page_cmd, sizeof(page_cmd));
        if (err != ESP_OK) {
            return err;
        }

        err = data(&buffer_[page * width_], static_cast<size_t>(width_));
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}

void Ssd1306::clear(void)
{
    memset(buffer_, 0x00, buffer_size_);
}

void Ssd1306::fill(bool on)
{
    memset(buffer_, on ? 0xFF : 0x00, buffer_size_);
}

esp_err_t Ssd1306::display(bool on)
{
    if (!configured_) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t cmd = on ? kCmdDisplayOn : kCmdDisplayOff;
    return command(&cmd, 1);
}

esp_err_t Ssd1306::invert(bool enabled)
{
    if (!configured_) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t cmd = enabled ? kCmdInvertDisplay : kCmdNormalDisplay;
    return command(&cmd, 1);
}

esp_err_t Ssd1306::contrast(uint8_t value)
{
    if (!configured_) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t cmds[] = {kCmdContrast, value};
    return command(cmds, sizeof(cmds));
}

void Ssd1306::pixel(int x, int y, bool on)
{
    if (x < 0 || x >= width_ || y < 0 || y >= height_) {
        return;
    }

    const size_t index = static_cast<size_t>(x + ((y / 8) * width_));
    const uint8_t mask = static_cast<uint8_t>(1U << (y & 0x07));

    if (on) {
        buffer_[index] |= mask;
    } else {
        buffer_[index] &= static_cast<uint8_t>(~mask);
    }
}

void Ssd1306::hLine(int x0, int x1, int y, bool on)
{
    if (y < 0 || y >= height_) {
        return;
    }

    if (x0 > x1) {
        const int tmp = x0;
        x0 = x1;
        x1 = tmp;
    }

    if (x1 < 0 || x0 >= width_) {
        return;
    }

    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 >= width_) {
        x1 = width_ - 1;
    }

    for (int x = x0; x <= x1; ++x) {
        pixel(x, y, on);
    }
}

void Ssd1306::vLine(int x, int y0, int y1, bool on)
{
    if (x < 0 || x >= width_) {
        return;
    }

    if (y0 > y1) {
        const int tmp = y0;
        y0 = y1;
        y1 = tmp;
    }

    if (y1 < 0 || y0 >= height_) {
        return;
    }

    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 >= height_) {
        y1 = height_ - 1;
    }

    for (int y = y0; y <= y1; ++y) {
        pixel(x, y, on);
    }
}

void Ssd1306::rect(int x, int y, int width, int height, bool on)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    hLine(x, x + width - 1, y, on);
    hLine(x, x + width - 1, y + height - 1, on);
    vLine(x, y, y + height - 1, on);
    vLine(x + width - 1, y, y + height - 1, on);
}

void Ssd1306::fillRect(int x, int y, int width, int height, bool on)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    for (int row = y; row < (y + height); ++row) {
        hLine(x, x + width - 1, row, on);
    }
}

void Ssd1306::line(int x0, int y0, int x1, int y1, bool on)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (true) {
        pixel(x0, y0, on);
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
}

esp_err_t Ssd1306::drawChar(int x, int y, char c, bool fg, bool bg, int scale)
{
    char text[2] = {c, '\0'};
    return drawText(x, y, text, fg, bg, scale);
}

esp_err_t Ssd1306::drawText(int x, int y, const char *text, bool fg, bool bg, int scale)
{
    if (text == nullptr || scale <= 0 || x < 0 || y < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int width = textWidth(text, scale);
    if (width <= 0) {
        return ESP_OK;
    }
    if ((x + width) > width_ || (y + (7 * scale)) > height_) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; text[i] != '\0'; ++i) {
        const uint8_t *glyph = font5x7ForChar(text[i]);
        const int char_x = x + static_cast<int>(i) * (6 * scale);

        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                const bool on = (glyph[row] & (1U << (4 - col))) != 0U;
                fillRect(char_x + (col * scale), y + (row * scale), scale, scale, on ? fg : bg);
            }
        }

        if (i + 1U < strlen(text)) {
            fillRect(char_x + (5 * scale), y, scale, 7 * scale, bg);
        }
    }

    return ESP_OK;
}

esp_err_t Ssd1306::drawTextAligned(int y, const char *text, bool fg, bool bg, int scale, Ssd1306Align align)
{
    if (text == nullptr || scale <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int width = textWidth(text, scale);
    if (width <= 0) {
        return ESP_OK;
    }

    int x = 0;
    switch (align) {
        case SSD1306_ALIGN_LEFT:
            break;
        case SSD1306_ALIGN_CENTER:
            x = (width_ - width) / 2;
            break;
        case SSD1306_ALIGN_RIGHT:
            x = width_ - width;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    return drawText(x, y, text, fg, bg, scale);
}

esp_err_t Ssd1306::drawTextBox(int x, int y, int width, int height, const char *text, bool fg, bool bg, int scale, Ssd1306Align align)
{
    if (text == nullptr || width <= 0 || height <= 0 || scale <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int text_width = textWidth(text, scale);
    const int text_height = 7 * scale;
    if (text_width > width || text_height > height) {
        return ESP_ERR_INVALID_ARG;
    }

    fillRect(x, y, width, height, bg);

    int draw_x = x;
    switch (align) {
        case SSD1306_ALIGN_LEFT:
            break;
        case SSD1306_ALIGN_CENTER:
            draw_x = x + ((width - text_width) / 2);
            break;
        case SSD1306_ALIGN_RIGHT:
            draw_x = x + (width - text_width);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    const int draw_y = y + ((height - text_height) / 2);
    return drawText(draw_x, draw_y, text, fg, bg, scale);
}

esp_err_t Ssd1306::draw7SegChar(int x, int y, char c, int height, int thickness, bool fg, bool bg)
{
    if (height <= 0 || thickness <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int width = sevenSegGlyphWidth(height, thickness);
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

    fillRect(x, y, width, height, bg);

    if (c == ' ') {
        return ESP_OK;
    }

    if (c == '.') {
        fillRect(x + width - thickness, y + height - thickness, thickness, thickness, fg);
        return ESP_OK;
    }

    if (c == ':') {
        fillRect(x + (width / 2) - (thickness / 2), y + (height / 3), thickness, thickness, fg);
        fillRect(x + (width / 2) - (thickness / 2), y + ((height * 2) / 3), thickness, thickness, fg);
        return ESP_OK;
    }

    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
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
        case 'A': mask = 0x77; break;
        case 'B': mask = 0x7C; break;
        case 'C': mask = 0x39; break;
        case 'D': mask = 0x5E; break;
        case 'E': mask = 0x79; break;
        case 'F': mask = 0x71; break;
        case 'G': mask = 0x6F; break;
        case 'H': mask = 0x76; break;
        case 'I': mask = 0x06; break;
        case 'J': mask = 0x1E; break;
        case 'L': mask = 0x38; break;
        case 'N': mask = 0x54; break;
        case 'P': mask = 0x73; break;
        case 'R': mask = 0x50; break;
        case 'S': mask = 0x6D; break;
        case 'T': mask = 0x78; break;
        case 'U': mask = 0x3E; break;
        case 'Y': mask = 0x6E; break;
        case '-': mask = 0x40; break;
        default:
            return ESP_OK;
    }

    if ((mask & 0x01) != 0U) fillRect(hseg_x, top_y, hseg_w, thickness, fg);
    if ((mask & 0x02) != 0U) fillRect(right_x, upper_y, thickness, vseg_h, fg);
    if ((mask & 0x04) != 0U) fillRect(right_x, lower_y, thickness, vseg_h, fg);
    if ((mask & 0x08) != 0U) fillRect(hseg_x, bottom_y, hseg_w, thickness, fg);
    if ((mask & 0x10) != 0U) fillRect(left_x, lower_y, thickness, vseg_h, fg);
    if ((mask & 0x20) != 0U) fillRect(left_x, upper_y, thickness, vseg_h, fg);
    if ((mask & 0x40) != 0U) fillRect(hseg_x, mid_y, hseg_w, thickness, fg);

    return ESP_OK;
}

esp_err_t Ssd1306::draw7SegText(int x, int y, const char *text, int height, int thickness, bool fg, bool bg)
{
    if (text == nullptr || height <= 0 || thickness <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int width = sevenSegGlyphWidth(height, thickness);
    const int spacing = thickness;
    int cursor_x = x;

    for (size_t i = 0; text[i] != '\0'; ++i) {
        ESP_RETURN_ON_ERROR(draw7SegChar(cursor_x, y, text[i], height, thickness, fg, bg), "SSD1306", "7seg draw failed");
        cursor_x += width + spacing;
    }

    return ESP_OK;
}

esp_err_t Ssd1306::draw7SegBox(int x, int y, int width, int height, int thickness, const char *text, bool fg, bool bg, Ssd1306Align align)
{
    if (text == nullptr || width <= 0 || height <= 0 || thickness <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int text_width = sevenSegTextWidth(text, height, thickness);
    if (text_width <= 0 || text_width > width) {
        return ESP_ERR_INVALID_ARG;
    }

    fillRect(x, y, width, height, bg);

    int draw_x = x;
    switch (align) {
        case SSD1306_ALIGN_LEFT:
            break;
        case SSD1306_ALIGN_CENTER:
            draw_x = x + ((width - text_width) / 2);
            break;
        case SSD1306_ALIGN_RIGHT:
            draw_x = x + (width - text_width);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    return draw7SegText(draw_x, y, text, height, thickness, fg, bg);
}

esp_err_t Ssd1306::drawProgressBar(int x, int y, int width, int height, int min, int max, int value, bool border, bool fill_on, bool bg)
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

    if (border) {
        rect(x, y, width, height, true);
    }
    fillRect(inner_x, inner_y, inner_w, inner_h, bg);
    if (fill_w > 0) {
        fillRect(inner_x, inner_y, fill_w, inner_h, fill_on);
    }

    return ESP_OK;
}

esp_err_t Ssd1306::updateTextBoxIfChanged(Ssd1306TextBoxState *box, const char *text)
{
    if (box == nullptr || text == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(box->last_text, text) == 0) {
        return ESP_OK;
    }
    if (!copyText(box->last_text, sizeof(box->last_text), text)) {
        return ESP_ERR_INVALID_ARG;
    }
    return drawTextBox(box->x, box->y, box->width, box->height, text, box->fg, box->bg, box->scale, box->align);
}

esp_err_t Ssd1306::update7SegBoxIfChanged(Ssd1306SevenSegBoxState *box, const char *text)
{
    if (box == nullptr || text == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(box->last_text, text) == 0) {
        return ESP_OK;
    }
    if (!copyText(box->last_text, sizeof(box->last_text), text)) {
        return ESP_ERR_INVALID_ARG;
    }
    return draw7SegBox(box->x, box->y, box->width, box->height, box->thickness, text, box->fg, box->bg, box->align);
}

esp_err_t Ssd1306::updateProgressBarIfChanged(Ssd1306ProgressBarState *bar, int value)
{
    if (bar == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (bar->last_value == value) {
        return ESP_OK;
    }
    bar->last_value = value;
    return drawProgressBar(bar->x, bar->y, bar->width, bar->height, bar->min, bar->max, value, bar->border, bar->fill, bar->bg);
}

esp_err_t Ssd1306::bitmap(int x, int y, int width, int height, const uint8_t *data, size_t len)
{
    if (data == nullptr || width <= 0 || height <= 0 || (height % 8) != 0 || (y % 8) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t expected = static_cast<size_t>(width * height / 8);
    if (len != expected) {
        return ESP_ERR_INVALID_SIZE;
    }

    const int page_count = (height / 8);
    for (int page = 0; page < page_count; ++page) {
        for (int col = 0; col < width; ++col) {
            const int dst_x = x + col;
            const int dst_page = (y / 8) + page;

            if (dst_x < 0 || dst_x >= width_ || dst_page < 0 || dst_page >= pages_) {
                continue;
            }

            const size_t src_index = static_cast<size_t>(col + (page * width));
            const size_t dst_index = static_cast<size_t>(dst_x + (dst_page * width_));
            buffer_[dst_index] = data[src_index];
        }
    }

    return ESP_OK;
}

int Ssd1306::width(void) const
{
    return width_;
}

int Ssd1306::height(void) const
{
    return height_;
}

int Ssd1306::pages(void) const
{
    return pages_;
}

uint16_t Ssd1306::address(void) const
{
    return address_;
}

int Ssd1306::port(void) const
{
    return port_;
}

int Ssd1306::textWidth(const char *text, int scale) const
{
    if (text == nullptr || scale <= 0) {
        return 0;
    }

    int glyphs = 0;
    for (size_t i = 0; text[i] != '\0'; ++i) {
        ++glyphs;
    }

    if (glyphs == 0) {
        return 0;
    }

    return (glyphs * (5 * scale)) + ((glyphs - 1) * scale);
}

int Ssd1306::sevenSegTextWidth(const char *text, int height, int thickness) const
{
    if (text == nullptr || height <= 0 || thickness <= 0) {
        return 0;
    }

    const int width = sevenSegGlyphWidth(height, thickness);
    const int spacing = thickness;
    int glyphs = 0;

    for (size_t i = 0; text[i] != '\0'; ++i) {
        ++glyphs;
    }

    if (glyphs == 0) {
        return 0;
    }

    return (glyphs * (width + spacing)) - spacing;
}

const uint8_t *Ssd1306::buffer(void) const
{
    return buffer_;
}

size_t Ssd1306::bufferSize(void) const
{
    return buffer_size_;
}

Ssd1306 ssd1306;

} // namespace esp_ssd1306
