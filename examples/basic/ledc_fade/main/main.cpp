#include "esp32libfun.hpp"

constexpr int PwmPin = 43;
constexpr uint32_t PwmFreqHz = 1000;
constexpr uint8_t PwmResolutionBits = 10;
constexpr uint32_t FadeMs = 1000;

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(serial.at(true));
    ESP_ERROR_CHECK(ledc.begin(PwmPin, PwmFreqHz, PwmResolutionBits));
    ESP_ERROR_CHECK(ledc.at(true));

    serial.println(M "Hello from ESP32LibFun!");
    serial.println(B "LEDC fade");
    serial.println(C "PWM pin=%d freq=%luHz resolution=%u-bit",
                   PwmPin,
                   static_cast<unsigned long>(PwmFreqHz),
                   static_cast<unsigned>(PwmResolutionBits));
    serial.println(C "AT ready: " W "AT+LEDC?%d" W ", " W "AT+LEDC=43,50", PwmPin);

    while (true) {
        serial.println(C "fade 0 -> %lu", static_cast<unsigned long>(ledc.maxDuty(PwmPin)));
        ESP_ERROR_CHECK(ledc.fade(PwmPin, ledc.maxDuty(PwmPin), FadeMs, true));
        delay.ms(250);

        serial.println(C "fade %lu -> 0", static_cast<unsigned long>(ledc.maxDuty(PwmPin)));
        ESP_ERROR_CHECK(ledc.fade(PwmPin, 0, FadeMs, true));
        delay.ms(250);
    }
}
