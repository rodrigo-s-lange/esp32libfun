#include "esp32libfun.hpp"

constexpr int WsPin = 43;
constexpr uint32_t WsResolutionHz = 10000000; // 10MHz -> 100ns per tick
constexpr uint32_t RepeatMs = 500;

// WS2812-style bit timing: bit0 = 400ns high / 800ns low, bit1 = 800ns high / 400ns low.
constexpr rmt_symbol_t Bit0 = {.duration0 = 4, .level0 = 1, .duration1 = 8, .level1 = 0};
constexpr rmt_symbol_t Bit1 = {.duration0 = 8, .level0 = 1, .duration1 = 4, .level1 = 0};

// One pixel, GRB order, black (all zero bits) -- scope test only, no LED expected to light.
constexpr uint8_t Pixel[] = {0x00, 0x00, 0x00};

// Reset/latch: hold the line low for 80us so the strip commits the frame.
constexpr rmt_symbol_t ResetSymbol = {.duration0 = 400, .level0 = 0, .duration1 = 400, .level1 = 0};

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(serial.at(true));

    ESP_ERROR_CHECK(rmt.beginTx(WsPin, WsResolutionHz));
    ESP_ERROR_CHECK(rmt.bits(WsPin, Bit0, Bit1));

    serial.println(M "Hello from ESP32LibFun!");
    serial.println(B "RMT WS2812 1-pixel proof of concept");
    serial.println(C "Output pin=%d resolution=%luHz", WsPin, static_cast<unsigned long>(WsResolutionHz));
    serial.println(C "Pixel (GRB): 0x%02X 0x%02X 0x%02X (black, all bit0)", Pixel[0], Pixel[1], Pixel[2]);
    serial.println(C "Scope: expect 24x bit0 pulses (~400ns high/800ns low), then >=80us low reset");

    while (true) {
        ESP_ERROR_CHECK(rmt.write(WsPin, Pixel, sizeof(Pixel)));
        ESP_ERROR_CHECK(rmt.writeSymbols(WsPin, &ResetSymbol, 1));
        delay.ms(RepeatMs);
    }
}
