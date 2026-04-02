#include "esp32libfun.hpp"

extern "C" void app_main(void)
{
    esp32libfun_init();

    while (true) {
        delay.s(1);
    }
}
