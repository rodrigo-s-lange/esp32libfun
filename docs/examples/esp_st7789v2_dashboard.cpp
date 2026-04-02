#include "esp32libfun.hpp"
#include "esp_st7789v2.hpp"

#include <stdio.h>

namespace {

constexpr int pinMosi = 4;
constexpr int pinSclk = 5;
constexpr int pinRst = 6;
constexpr int pinDc = 7;
constexpr int pinCs = 15;

#if SOC_SPI_PERIPH_NUM > 2
constexpr int displaySpiPort = SPI_HOST_3;
#else
constexpr int displaySpiPort = SPI_HOST_DEFAULT;
#endif

struct Swatch {
    const char *name;
    uint16_t color;
    uint16_t text;
};

constexpr Swatch swatches[] = {
    {"BLACK", BLACK, WHITE},
    {"WHITE", WHITE, BLACK},
    {"RED", RED, WHITE},
    {"GREEN", GREEN, BLACK},
    {"LIME", LIME, BLACK},
    {"BLUE", BLUE, WHITE},
    {"YELLOW", YELLOW, BLACK},
    {"CYAN", CYAN, BLACK},
    {"MAGENTA", MAGENTA, WHITE},
    {"ORANGE", ORANGE, BLACK},
    {"PINK", PINK, BLACK},
    {"DARK_PINK", DARK_PINK, WHITE},
    {"GOLD", GOLD, BLACK},
    {"GRAY", GRAY, WHITE},
    {"LIGHT_GRAY", LIGHT_GRAY, BLACK},
    {"DARK_GRAY", DARK_GRAY, WHITE},
    {"NAVY", NAVY, WHITE},
    {"DEEP_BLUE", DEEP_BLUE, WHITE},
    {"SKY_BLUE", SKY_BLUE, BLACK},
    {"TEAL", TEAL, WHITE},
    {"MAROON", MAROON, WHITE},
    {"OLIVE", OLIVE, WHITE},
};

constexpr int pageSize = 6;

St7789v2SevenSegBoxState tempBox {
    .x = 16,
    .y = 38,
    .width = 132,
    .height = 42,
    .thickness = 6,
    .fg = CYAN,
    .bg = DEEP_BLUE,
    .align = ST7789V2_ALIGN_CENTER,
    .initialized = true,
};

St7789v2SevenSegBoxState humBox {
    .x = 172,
    .y = 38,
    .width = 132,
    .height = 42,
    .thickness = 6,
    .fg = GOLD,
    .bg = MAROON,
    .align = ST7789V2_ALIGN_CENTER,
    .initialized = true,
};

St7789v2ProgressBarState tempBar {
    .x = 16,
    .y = 118,
    .width = 132,
    .height = 16,
    .min = 0,
    .max = 50,
    .value = -1,
    .border_color = CYAN,
    .fill_color = SKY_BLUE,
    .bg_color = BLACK,
    .initialized = true,
};

St7789v2ProgressBarState humBar {
    .x = 172,
    .y = 118,
    .width = 132,
    .height = 16,
    .min = 0,
    .max = 100,
    .value = -1,
    .border_color = GOLD,
    .fill_color = ORANGE,
    .bg_color = BLACK,
    .initialized = true,
};

St7789v2TextBoxState statusBox {
    .x = 18,
    .y = 146,
    .width = 284,
    .height = 18,
    .scale = 1,
    .fg = WHITE,
    .bg = BLACK,
    .align = ST7789V2_ALIGN_CENTER,
    .initialized = true,
};

esp_err_t drawPalettePage(int start_index)
{
    constexpr int header_h = 20;
    constexpr int footer_h = 14;
    constexpr int gap = 6;
    constexpr int columns = 2;
    constexpr int card_w = 150;
    constexpr int card_h = 42;
    constexpr int left_margin = 7;
    constexpr int top_margin = 26;

    ESP_ERROR_CHECK(st7789v2.fillScreen(BLACK));
    ESP_ERROR_CHECK(st7789v2.drawTextBox(0,
                                         0,
                                         st7789v2.width(),
                                         header_h,
                                         "CALIBRATED PALETTE",
                                         WHITE,
                                         BLACK,
                                         1,
                                         ST7789V2_ALIGN_CENTER));

    for (int i = 0; i < pageSize; ++i) {
        const int index = start_index + i;
        if (index >= static_cast<int>(sizeof(swatches) / sizeof(swatches[0]))) {
            break;
        }

        const int row = i / columns;
        const int col = i % columns;
        const int x = left_margin + (col * (card_w + gap));
        const int y = top_margin + (row * (card_h + gap));
        const Swatch &swatch = swatches[index];

        ESP_ERROR_CHECK(st7789v2.fillRoundRect(x, y, card_w, card_h, 8, swatch.color));
        ESP_ERROR_CHECK(st7789v2.drawRoundRect(x, y, card_w, card_h, 8, swatch.text));
        ESP_ERROR_CHECK(st7789v2.drawTextBox(x + 6,
                                             y + 5,
                                             card_w - 12,
                                             14,
                                             swatch.name,
                                             swatch.text,
                                             swatch.color,
                                             1,
                                             ST7789V2_ALIGN_CENTER));
        ESP_ERROR_CHECK(st7789v2.drawProgressBar(x + 12,
                                                 y + 25,
                                                 card_w - 24,
                                                 10,
                                                 0,
                                                 100,
                                                 100,
                                                 swatch.text,
                                                 swatch.text,
                                                 swatch.color));
    }

    char footer[24] = {};
    const int page = (start_index / pageSize) + 1;
    const int total_pages = (static_cast<int>(sizeof(swatches) / sizeof(swatches[0])) + pageSize - 1) / pageSize;
    snprintf(footer, sizeof(footer), "PAGE %d/%d", page, total_pages);
    ESP_ERROR_CHECK(st7789v2.drawTextBox(0,
                                         st7789v2.height() - footer_h,
                                         st7789v2.width(),
                                         footer_h,
                                         footer,
                                         GRAY,
                                         BLACK,
                                         1,
                                         ST7789V2_ALIGN_CENTER));
    return ESP_OK;
}

esp_err_t drawDashboardFrame(void)
{
    ESP_ERROR_CHECK(st7789v2.fillScreen(BLACK));
    ESP_ERROR_CHECK(st7789v2.fillRoundRect(8, 8, 304, 154, 12, NAVY));
    ESP_ERROR_CHECK(st7789v2.drawRoundRect(8, 8, 304, 154, 12, SKY_BLUE));
    ESP_ERROR_CHECK(st7789v2.drawTextBox(0,
                                         12,
                                         st7789v2.width(),
                                         16,
                                         "ST7789V2 DASHBOARD",
                                         WHITE,
                                         NAVY,
                                         1,
                                         ST7789V2_ALIGN_CENTER));
    ESP_ERROR_CHECK(st7789v2.drawGrid(12, 32, 296, 106, 2, 2, DARK_GRAY));
    ESP_ERROR_CHECK(st7789v2.drawTextBox(16, 30, 132, 10, "TEMP", CYAN, NAVY, 1, ST7789V2_ALIGN_CENTER));
    ESP_ERROR_CHECK(st7789v2.drawTextBox(172, 30, 132, 10, "HUM", GOLD, NAVY, 1, ST7789V2_ALIGN_CENTER));
    ESP_ERROR_CHECK(st7789v2.drawCircle(42, 102, 12, CYAN));
    ESP_ERROR_CHECK(st7789v2.fillCircle(42, 102, 8, SKY_BLUE));
    ESP_ERROR_CHECK(st7789v2.drawTriangle(278, 88, 300, 120, 256, 120, GOLD));
    ESP_ERROR_CHECK(st7789v2.fillTriangle(278, 94, 292, 116, 264, 116, ORANGE));
    return ESP_OK;
}

esp_err_t updateDashboard(int step)
{
    const int temp = 18 + ((step * 3) % 17);
    const int hum = 42 + ((step * 7) % 45);
    const int pulse = (step * 13) % 100;

    char temp_text[16] = {};
    char hum_text[16] = {};
    char status_text[32] = {};

    snprintf(temp_text, sizeof(temp_text), "%02dO", temp);
    snprintf(hum_text, sizeof(hum_text), "%02d", hum);
    snprintf(status_text, sizeof(status_text), "pulse %02d  page-ready", pulse);

    ESP_ERROR_CHECK(st7789v2.update7SegBoxIfChanged(&tempBox, temp_text));
    ESP_ERROR_CHECK(st7789v2.update7SegBoxIfChanged(&humBox, hum_text));
    ESP_ERROR_CHECK(st7789v2.updateProgressBarIfChanged(&tempBar, temp));
    ESP_ERROR_CHECK(st7789v2.updateProgressBarIfChanged(&humBar, hum));
    ESP_ERROR_CHECK(st7789v2.updateTextBoxIfChanged(&statusBox, status_text));
    ESP_ERROR_CHECK(st7789v2.drawTextBox(106, 88, 24, 12, "C", CYAN, DEEP_BLUE, 1, ST7789V2_ALIGN_CENTER));
    ESP_ERROR_CHECK(st7789v2.drawTextBox(262, 88, 20, 12, "%", GOLD, MAROON, 1, ST7789V2_ALIGN_CENTER));
    return ESP_OK;
}

} // namespace

extern "C" void app_main(void)
{
    esp32libfun_init();

    serial.println(O "[st7789v2-demo]" C " calibrated palette demo");
    serial.println(O "[st7789v2-demo]" C " spi port " O "%d" C " at " O "%lu" C " Hz",
                   displaySpiPort,
                   static_cast<unsigned long>(St7789v2::DEFAULT_CLOCK_HZ));

    ESP_ERROR_CHECK(spi.begin(pinSclk, pinMosi, -1, displaySpiPort));
    ESP_ERROR_CHECK(st7789v2.init(pinCs, pinDc, pinRst, -1, displaySpiPort));
    ESP_ERROR_CHECK(st7789v2.setRotation(ST7789V2_ROTATION_180));

    while (true) {
        serial.println(O "[st7789v2-demo]" C " dashboard scene");
        ESP_ERROR_CHECK(drawDashboardFrame());
        for (int step = 0; step < 8; ++step) {
            ESP_ERROR_CHECK(updateDashboard(step));
            delay.ms(450);
        }

        for (int start = 0; start < static_cast<int>(sizeof(swatches) / sizeof(swatches[0])); start += pageSize) {
            serial.println(O "[st7789v2-demo]" C " palette page starting at index " O "%d", start);
            ESP_ERROR_CHECK(drawPalettePage(start));
            delay.s(2);
        }
    }
}
