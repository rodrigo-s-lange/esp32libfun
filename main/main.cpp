#include "esp32libfun.hpp"

extern "C" void app_main(void)
{
    esp32libfun_init();
    serial.println(M "Hello from ESP32LibFun!");
}
