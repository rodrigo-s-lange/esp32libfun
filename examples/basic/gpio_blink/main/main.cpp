#include "esp32libfun.hpp"

constexpr int BlinkPin = 43;

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(serial.at(true));
    ESP_ERROR_CHECK(gpio.cfg(BlinkPin, OUTPUT));
    ESP_ERROR_CHECK(gpio.at(true));

    serial.println(M "Hello from ESP32LibFun!");
    serial.println(B "GPIO blink");
    serial.println(C "Blink pin = %d", BlinkPin);
    serial.println(C "AT ready: " W "AT+GPIO?%d" W ", " W "AT+GPIOTOGGLE=%d", BlinkPin, BlinkPin);

    while (true) {
        ESP_ERROR_CHECK(gpio.toggle(BlinkPin));
        serial.println(C "GPIO%d=%d", BlinkPin, gpio.state(BlinkPin) ? 1 : 0);
        delay.s(1);
    }
}
