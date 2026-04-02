#include "esp32libfun.hpp"
#include "esp_bmp280.hpp"

constexpr int      kSda          = 4;
constexpr int      kScl          = 5;
constexpr uint32_t kIntervalMs   = 2000;

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(i2c.begin(kSda, kScl, I2C_FAST));
    ESP_ERROR_CHECK(bmp280.init());

    while (true) {
        ESP_ERROR_CHECK(bmp280.loop());
        serial.println(C "BMP280  temp: " O "%.2f C" C "  press: " O "%.2f hPa",
                       bmp280.temperature(), bmp280.pressure());
        delay.ms(kIntervalMs);
    }
}
