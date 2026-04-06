#include "esp32libfun.hpp"
#include "esp_si7021.hpp"

constexpr int      kSda        = 4;
constexpr int      kScl        = 5;
constexpr uint32_t kIntervalMs = 2000;

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(i2c.begin(kSda, kScl, I2C_FAST));
    ESP_ERROR_CHECK(si7021.init());

    while (true) {
        ESP_ERROR_CHECK(si7021.loop());
        serial.println(C "Si7021  temp: " O "%.2f C" C "  hum: " O "%.2f %%",
                       si7021.temperature(), si7021.humidity());
        delay.ms(kIntervalMs);
    }
}
