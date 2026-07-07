#include "esp32libfun.hpp"

constexpr int AdcPin = 4;
constexpr uint32_t SampleMs = 500;

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(serial.at(true));
    ESP_ERROR_CHECK(adc.begin(AdcPin));

    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;
    ESP_ERROR_CHECK(adc.channel(AdcPin, &unit, &channel));

    serial.println(M "Hello from ESP32LibFun!");
    serial.println(B "ADC oneshot test");
    serial.println(C "GPIO%d -> unit=%d channel=%d", AdcPin, static_cast<int>(unit), static_cast<int>(channel));
    serial.println(C "AT ready: " W "AT" W ", " W "AT+HELP?");

    while (true) {
        int raw = 0;
        int mv = 0;
        esp_err_t raw_err = adc.read(AdcPin, &raw);
        esp_err_t mv_err = adc.voltage(AdcPin, &mv);

        if (mv_err == ESP_OK) {
            serial.println(C "ADC GPIO%d raw=%d mv=%d", AdcPin, raw, mv);
        } else {
            serial.println(C "ADC GPIO%d raw=%d raw_err=%s mv_err=%s",
                           AdcPin,
                           raw,
                           esp_err_to_name(raw_err),
                           esp_err_to_name(mv_err));
        }
        delay.ms(SampleMs);
    }
}
