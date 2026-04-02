#include <stdio.h>

#include "esp32libfun.hpp"
#include "esp_ssd1306.hpp"

namespace {

constexpr int i2cSdaPin = 8;
constexpr int i2cSclPin = 9;
constexpr int i2cPort = 0;
constexpr uint32_t i2cSpeedHz = I2C_FAST;
constexpr uint32_t frameMs = 120;

void drawDashboardFrame(void)
{
    ssd1306.clear();
    // This common bicolor module has yellow pixels on y=0..15 and blue below.
    ssd1306.rect(0, 0, ssd1306.width(), 16, true);
    ssd1306.drawTextBox(2, 3, 124, 8, "SSD1306 DASH", true, false, 1, SSD1306_ALIGN_CENTER);
    ssd1306.rect(0, 16, 64, 22, true);
    ssd1306.rect(64, 16, 64, 22, true);
    ssd1306.drawTextBox(8, 40, 34, 8, "TEMP", true, false, 1, SSD1306_ALIGN_LEFT);
    ssd1306.drawTextBox(86, 40, 34, 8, "HUM", true, false, 1, SSD1306_ALIGN_RIGHT);
}

} // namespace

extern "C" void app_main(void)
{
    esp32libfun_init();

    serial.println(O "SSD1306 dashboard demo");
    ESP_ERROR_CHECK(i2c.begin(i2cSdaPin, i2cSclPin, i2cSpeedHz, i2cPort));
    ESP_ERROR_CHECK(ssd1306.init(Ssd1306::DEFAULT_ADDRESS, i2cPort));

    int step = 0;
    while (true) {
        const int temp = 18 + ((step * 3) % 18);
        const int hum = 35 + ((step * 7) % 55);

        char tempText[8] = {};
        char humText[8] = {};
        snprintf(tempText, sizeof(tempText), "%02d", temp);
        snprintf(humText, sizeof(humText), "%02d", hum);

        drawDashboardFrame();
        ESP_ERROR_CHECK(ssd1306.draw7SegBox(2, 18, 60, 18, 2, tempText, true, false, SSD1306_ALIGN_CENTER));
        ESP_ERROR_CHECK(ssd1306.draw7SegBox(66, 18, 60, 18, 2, humText, true, false, SSD1306_ALIGN_CENTER));
        ESP_ERROR_CHECK(ssd1306.drawProgressBar(8, 51, 112, 5, 0, 50, temp, true, true, false));
        ESP_ERROR_CHECK(ssd1306.drawProgressBar(8, 58, 112, 5, 0, 100, hum, true, true, false));

        ssd1306.drawText(46, 40, "C", true, false, 1);
        ssd1306.drawText(120, 40, "%", true, false, 1);

        ESP_ERROR_CHECK(ssd1306.present());

        serial.println(G "SSD1306 temp=" C "%d" G " hum=" C "%d", temp, hum);
        ++step;
        delay.ms(frameMs);
    }
}
