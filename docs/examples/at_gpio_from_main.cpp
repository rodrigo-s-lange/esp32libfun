#include <string.h>

#include "esp32libfun.hpp"

namespace {

constexpr int gpioPin = 8;

void atGpio8Set(const char *args)
{
    ESP_ERROR_CHECK(gpio.cfg(gpioPin, OUTPUT));

    if (strcmp(args, "1") == 0 || strcmp(args, "ON") == 0 || strcmp(args, "HIGH") == 0) {
        ESP_ERROR_CHECK(gpio.high(gpioPin));
        at.writeLine(G "OK" W);
        return;
    }

    if (strcmp(args, "0") == 0 || strcmp(args, "OFF") == 0 || strcmp(args, "LOW") == 0) {
        ESP_ERROR_CHECK(gpio.low(gpioPin));
        at.writeLine(G "OK" W);
        return;
    }

    at.writeError(R "use 0/1 or OFF/ON or LOW/HIGH");

}

void atGpio8Get(const char *args)
{
    (void) args;
    at.writeLine("GPIO8=%d", gpio.state(gpioPin) ? 1 : 0);
}

} // namespace

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(gpio.cfg(gpioPin, OUTPUT));
    ESP_ERROR_CHECK(at.registerCmd("AT+GPIO8", atGpio8Set, "GPIO 8 control: 0|1|OFF|ON"));
    ESP_ERROR_CHECK(at.registerCmd("AT+GPIO8?", atGpio8Get, "Read GPIO 8 output state"));

    serial.println(O "Try:" W " AT+GPIO8=1" W ", " W "AT+GPIO8=0" W ", " W "AT+GPIO8?" W);

    while (true) {
        delay.s(1);
    }
}
