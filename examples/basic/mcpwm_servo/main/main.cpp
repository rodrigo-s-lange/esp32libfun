#include "esp32libfun.hpp"

constexpr int ServoPin = 43;
constexpr uint32_t ServoPeriodUs = 20000;
constexpr uint32_t ServoHoldMs = 3000;

constexpr uint32_t ServoPulsesUs[] = {
    1000,
    1500,
    2000,
};

void setServoPulse(uint32_t high_us)
{
    ESP_ERROR_CHECK(mcpwm.pulse(ServoPin, high_us, ServoPeriodUs));
    serial.println(C "MCPWM pin=%d pulse=%luus period=%luus freq=%luHz duty=%.2f%%",
                   ServoPin,
                   static_cast<unsigned long>(high_us),
                   static_cast<unsigned long>(ServoPeriodUs),
                   static_cast<unsigned long>(mcpwm.freq(ServoPin)),
                   static_cast<double>(mcpwm.duty(ServoPin)));
}

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(serial.at(true));
    ESP_ERROR_CHECK(mcpwm.begin(ServoPin, MCPWM_SERVO, 0.0f));
    ESP_ERROR_CHECK(mcpwm.pulse(ServoPin, ServoPulsesUs[0], ServoPeriodUs));

    serial.println(M "Hello from ESP32LibFun!");
    serial.println(B "MCPWM servo sweep test");
    serial.println(C "Output pin=%d", ServoPin);
    serial.println(C "Expected: 50Hz period=%luus pulses=1000/1500/2000us",
                   static_cast<unsigned long>(ServoPeriodUs));
    serial.println(C "AT ready: " W "AT" W ", " W "AT+HELP?");

    while (true) {
        for (uint32_t high_us : ServoPulsesUs) {
            setServoPulse(high_us);
            delay.ms(ServoHoldMs);
        }
    }
}
