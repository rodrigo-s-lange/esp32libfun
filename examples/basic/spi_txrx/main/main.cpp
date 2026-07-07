#include "esp32libfun.hpp"

constexpr int SclkPin = 4;
constexpr int CsPin = 5;
constexpr int MosiPin = 6;
constexpr int MisoPin = 7;
constexpr int SpiPort = SPI_HOST_DEFAULT;
constexpr uint32_t SpiClockHz = 1000000;

void printHex(const char *label, const uint8_t *data, size_t len)
{
    serial.print(C "%s=", label);
    for (size_t i = 0; i < len; ++i) {
        serial.print(C "%02X", data[i]);
    }
}

void runSpiTransfer(uint8_t counter)
{
    uint8_t tx[] = { 0xA5, 0x5A, counter, static_cast<uint8_t>(counter + 1U), 0xC3, 0x3C };
    uint8_t rx[sizeof(tx)] = {};

    ESP_ERROR_CHECK(spi.transfer(CsPin, tx, rx, sizeof(tx), SpiPort));

    printHex("tx", tx, sizeof(tx));
    serial.print(" ");
    printHex("rx", rx, sizeof(rx));
    serial.print("\r\n");
}

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(serial.at(true));
    ESP_ERROR_CHECK(spi.begin(SclkPin, MosiPin, MisoPin, SpiPort));
    ESP_ERROR_CHECK(spi.add(CsPin, SpiClockHz, SPI_MODE_0, SpiPort));
    ESP_ERROR_CHECK(spi.at(true));

    serial.println(M "Hello from ESP32LibFun!");
    serial.println(B "SPI TX/RX");
    serial.println(C "SCLK=%d MOSI=%d MISO=%d CS=%d clock=%luHz",
                   SclkPin,
                   MosiPin,
                   MisoPin,
                   CsPin,
                   static_cast<unsigned long>(SpiClockHz));
    serial.println(C "AT ready: " W "AT+SPIBUS?%d" W ", " W "AT+SPITXRX=%d,A55A00010203C33C", SpiPort, CsPin);

    uint8_t counter = 0;
    while (true) {
        runSpiTransfer(counter++);
        delay.s(1);
    }
}
