#include "esp32libfun.hpp"

int x = 0;

extern "C" void app_main(void)
{
    esp32libfun_init();
    while (true) {
        delay.s(10);
        serial.println(C "X = %d",x);
        x++;               
    }
    
}
