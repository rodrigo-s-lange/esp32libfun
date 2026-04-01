#include "esp32libfun.hpp"
#include "esp_si7021.hpp"

constexpr int kSda = 4;
constexpr int kScl = 5;

static void onSi7021Read(Si7021 &s)
{
    serial.println(C "Si7021  temp: " O "%.2f °C" C "  hum: " O "%.2f %%",
                   s.temperature(), s.humidity());
}

extern "C" void app_main(void)
{
    esp32libfun_init();

    i2c.begin(kSda, kScl, I2C_FAST);
    si7021.init();
    si7021.onRead(onSi7021Read);
    si7021.start();

    while (true) {
        delay.s(1);
    }
}
