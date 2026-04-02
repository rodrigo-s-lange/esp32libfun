#include "esp32libfun.hpp"
#include "esp_ssd1306.hpp"

namespace {

constexpr int sdaPin = 8;
constexpr int sclPin = 9;
constexpr uint32_t sceneIntervalMs = 2000;

void drawSceneA(void)
{
    ssd1306.clear();
    ssd1306.rect(0, 0, ssd1306.width(), ssd1306.height());
    ssd1306.rect(8, 8, ssd1306.width() - 16, ssd1306.height() - 16);
}

void drawSceneB(void)
{
    ssd1306.clear();
    ssd1306.fillRect(16, 16, 96, 32, true);
    ssd1306.rect(24, 20, 80, 24, false);
}

} // namespace

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(i2c.begin(sdaPin, sclPin, I2C_FAST));
    ESP_ERROR_CHECK(ssd1306.init());

    while (true) {
        drawSceneA();
        ESP_ERROR_CHECK(ssd1306.present());
        delay.ms(sceneIntervalMs);

        drawSceneB();
        ESP_ERROR_CHECK(ssd1306.present());
        delay.ms(sceneIntervalMs);
    }
}
