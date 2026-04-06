#include "esp32libfun.hpp"
#include "esp_bmp280.hpp"

constexpr int kSda = 4;
constexpr int kScl = 5;

static void onBmp280Read(Bmp280 &b)
{
    serial.println(C "BMP280  temp: " O "%.2f C" C "  press: " O "%.2f hPa",
                   b.temperature(), b.pressure());
}

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(i2c.begin(kSda, kScl, I2C_FAST));
    ESP_ERROR_CHECK(bmp280.init());
    ESP_ERROR_CHECK(bmp280.onRead(onBmp280Read));
    ESP_ERROR_CHECK(bmp280.start());

    while (true) {
        delay.s(1);
    }
}
