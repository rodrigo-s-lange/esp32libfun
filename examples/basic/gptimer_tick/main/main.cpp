#include "esp32libfun.hpp"

constexpr int TimerId = 0;
constexpr uint64_t PeriodUs = 1000000;

void onTimer(int timer, uint64_t count, void *user_ctx)
{
    (void)user_ctx;
    serial.println(C "alarm timer=%d count=%llu micros=%llu",
                   timer,
                   static_cast<unsigned long long>(count),
                   static_cast<unsigned long long>(gptimer.micros(timer)));
}

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(serial.at(true));
    ESP_ERROR_CHECK(gptimer.begin(TimerId));
    ESP_ERROR_CHECK(gptimer.alarm(TimerId, PeriodUs, onTimer));

    serial.println(M "Hello from ESP32LibFun!");
    serial.println(B "GPTimer tick test");
    serial.println(C "timer=%d resolution=%luHz period=%lluus",
                   TimerId,
                   static_cast<unsigned long>(gptimer.resolution(TimerId)),
                   static_cast<unsigned long long>(PeriodUs));
    serial.println(C "AT ready: " W "AT" W ", " W "AT+HELP?");

    while (true) {
        serial.println(C "poll micros=%llu millis=%llu",
                       static_cast<unsigned long long>(gptimer.micros(TimerId)),
                       static_cast<unsigned long long>(gptimer.millis(TimerId)));
        delay.s(5);
    }
}
