#include "esp32libfun.hpp"

uint64_t x = 0;

extern "C" void app_main(void)
{
    esp32libfun_init();
    serial.println(C "Hello from Libfun! Version: %s", ESP32LIBFUN_VERSION);
    while (true) {
        serial.println(O "Tick: " C "%llu", x++);
        delay.s(1);        
    }
}
