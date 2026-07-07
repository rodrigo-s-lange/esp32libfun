#include "esp32libfun.hpp"

constexpr int IrTxPin = 43;
constexpr uint32_t CarrierHz = 38000;
constexpr float CarrierDuty = 0.33f;
constexpr uint32_t RepeatMs = 500;

// NEC-style bit timing: 562us mark for both, 562us space for bit0, 1690us space for bit1.
constexpr rmt_symbol_t Bit0 = {.duration0 = 560, .level0 = 1, .duration1 = 560, .level1 = 0};
constexpr rmt_symbol_t Bit1 = {.duration0 = 560, .level0 = 1, .duration1 = 1690, .level1 = 0};

constexpr uint8_t Payload[] = {0xA5, 0x3C};

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(serial.at(true));

    ESP_ERROR_CHECK(rmt.beginTx(IrTxPin));
    ESP_ERROR_CHECK(rmt.carrier(IrTxPin, CarrierHz, CarrierDuty));
    ESP_ERROR_CHECK(rmt.bits(IrTxPin, Bit0, Bit1));

    serial.println(M "Hello from ESP32LibFun!");
    serial.println(B "RMT 38kHz IR TX validation");
    serial.println(C "Output pin=%d carrier=%luHz duty=%.0f%%",
                   IrTxPin,
                   static_cast<unsigned long>(CarrierHz),
                   static_cast<double>(CarrierDuty * 100));
    serial.println(C "Payload bytes: 0x%02X 0x%02X (MSB first)", Payload[0], Payload[1]);
    serial.println(C "Scope: expect ~38kHz carrier bursts, ~562us mark, ~562/1690us space per bit");
    serial.println(C "AT ready: " W "AT" W ", " W "AT+HELP?");

    while (true) {
        ESP_ERROR_CHECK(rmt.write(IrTxPin, Payload, sizeof(Payload)));
        delay.ms(RepeatMs);
    }
}
