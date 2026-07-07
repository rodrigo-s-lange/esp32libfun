#include "esp32libfun.hpp"

#include "driver/gpio.h"

constexpr int PulseOutPin = 43;
constexpr int PcntInputPin = 13;
constexpr uint8_t LedcResolutionBits = 1;
constexpr uint32_t MeasureWindowMs = 1000;
constexpr uint32_t SettleMs = 50;
constexpr int PcntLowLimit = -32768;
constexpr int PcntHighLimit = 32767;

constexpr uint32_t TestFrequenciesHz[] = {
    10000,
    30000,
    40000,
    100000,
    250000,
    500000,
    1000000,
};

void enablePcntInputPullup(void)
{
    const auto pin = static_cast<gpio_num_t>(PcntInputPin);
    ESP_ERROR_CHECK(gpio_pulldown_dis(pin));
    ESP_ERROR_CHECK(gpio_pullup_en(pin));
}

uint32_t expectedPulses(uint32_t freq_hz)
{
    return static_cast<uint32_t>((static_cast<uint64_t>(freq_hz) * MeasureWindowMs) / 1000ULL);
}

void runFrequencyTest(uint32_t freq_hz)
{
    ESP_ERROR_CHECK(ledc.duty(PulseOutPin, 0));
    delay.ms(SettleMs);

    ESP_ERROR_CHECK(ledc.freq(PulseOutPin, freq_hz));
    ESP_ERROR_CHECK(pcnt.clear(PcntInputPin));
    ESP_ERROR_CHECK(ledc.duty(PulseOutPin, 1));
    delay.ms(MeasureWindowMs);
    ESP_ERROR_CHECK(ledc.duty(PulseOutPin, 0));
    delay.ms(SettleMs);

    int count = 0;
    ESP_ERROR_CHECK(pcnt.count(PcntInputPin, &count));

    const uint32_t expected = expectedPulses(freq_hz);
    serial.println(C "LEDC=%luHz window=%lums expected=%lu counted=%d delta=%ld",
                   static_cast<unsigned long>(freq_hz),
                   static_cast<unsigned long>(MeasureWindowMs),
                   static_cast<unsigned long>(expected),
                   count,
                   static_cast<long>(count) - static_cast<long>(expected));
}

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(serial.at(true));
    ESP_ERROR_CHECK(gpio.cfg(PcntInputPin, INPUT_PULLUP));
    ESP_ERROR_CHECK(gpio.at(true));
    enablePcntInputPullup();

    ESP_ERROR_CHECK(pcnt.begin(PcntInputPin, PCNT_RISE, PcntLowLimit, PcntHighLimit, 0));
    enablePcntInputPullup();

    ESP_ERROR_CHECK(ledc.begin(PulseOutPin, TestFrequenciesHz[0], LedcResolutionBits));
    ESP_ERROR_CHECK(ledc.duty(PulseOutPin, 0));
    ESP_ERROR_CHECK(ledc.at(true));

    serial.println(M "Hello from ESP32LibFun!");
    serial.println(B "Teste PCNT com LEDC clock");
    serial.println(C "LEDC pulse pin = %d", PulseOutPin);
    serial.println(C "PCNT input pin = %d; connect GPIO%d -> GPIO%d", PcntInputPin, PulseOutPin, PcntInputPin);
    serial.println(C "LEDC resolution=%u-bit duty=50%% window=%lums",
                   static_cast<unsigned>(LedcResolutionBits),
                   static_cast<unsigned long>(MeasureWindowMs));
    serial.println(C "PCNT limits: %d..%d", PcntLowLimit, PcntHighLimit);
    serial.println(C "AT ready: " W "AT" W ", " W "AT+HELP?");

    while (true) {
        for (uint32_t freq_hz : TestFrequenciesHz) {
            runFrequencyTest(freq_hz);
        }
        serial.println(C "---");
        delay.s(2);
    }
}
