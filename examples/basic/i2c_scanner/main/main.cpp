#include "esp32libfun.hpp"

constexpr int SdaPin = 40;
constexpr int SclPin = 41;
constexpr int I2cPort = 0;
constexpr uint32_t I2cSpeedHz = I2C_STANDARD;

void scanI2cBus(void)
{
    int found = 0;
    serial.println(C "I2C scan start");

    for (uint16_t address = 0x08; address <= 0x77; ++address) {
        if (i2c.probe(address, I2cPort, 20) == ESP_OK) {
            serial.println(C "  found 0x%02X", address);
            ++found;
        }
    }

    serial.println(C "I2C scan done: %d device(s)", found);
}

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(serial.at(true));
    ESP_ERROR_CHECK(i2c.begin(SdaPin, SclPin, I2cSpeedHz, I2cPort));
    ESP_ERROR_CHECK(i2c.at(true));

    serial.println(M "Hello from ESP32LibFun!");
    serial.println(B "I2C scanner");
    serial.println(C "SDA=%d SCL=%d speed=%luHz port=%d",
                   SdaPin,
                   SclPin,
                   static_cast<unsigned long>(I2cSpeedHz),
                   I2cPort);
    serial.println(C "AT ready: " W "AT+I2CSCAN" W ", " W "AT+I2CBUS?0");

    while (true) {
        scanI2cBus();
        delay.s(5);
    }
}
