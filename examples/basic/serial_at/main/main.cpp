#include "esp32libfun.hpp"

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(serial.at(true));

    serial.println(M "Hello from ESP32LibFun!");
    serial.println(C "Version: " W "%s", ESP32LIBFUN_VERSION);
    serial.println(C "AT ready: " W "AT" W ", " W "AT+HELP?" W ", " W "AT+VER?");

    while (true) {
        serial.println(C "uptime loop");
        delay.s(5);
    }
}
