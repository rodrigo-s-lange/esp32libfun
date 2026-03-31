#include "esp32libfun.hpp"

namespace {

constexpr int kI2cSdaPin = 8;
constexpr int kI2cSclPin = 9;
constexpr uint32_t kI2cSpeedHz = I2C_FAST_PLUS;
constexpr uint32_t kScanIntervalMs = 3000;
constexpr int kProbeTimeoutMs = 20;
constexpr uint8_t kFirstAddress = 0x03;
constexpr uint8_t kLastAddress = 0x77;

void scanI2cBus(void)
{
    bool found_any = false;

    serial.println(O "Scanning I2C bus...");
    for (uint8_t address = kFirstAddress; address <= kLastAddress; ++address) {
        const esp_err_t err = i2c.probe(address, 0, kProbeTimeoutMs);
        if (err == ESP_OK) {
            serial.println(G "  Found device at " C "0x%02X", address);
            found_any = true;
        }
    }

    if (!found_any) {
        serial.println(Y "  No I2C devices found");
    }
}

} // namespace

extern "C" void app_main(void)
{
    esp32libfun_init();

    serial.println(O "I2C scanner example");
    serial.println(O "SDA=" C "%d" O " SCL=" C "%d" O " Speed=" C "%lu Hz",
                   kI2cSdaPin,
                   kI2cSclPin,
                   static_cast<unsigned long>(kI2cSpeedHz));

    ESP_ERROR_CHECK(i2c.begin(kI2cSdaPin, kI2cSclPin, kI2cSpeedHz));

    while (true) {
        scanI2cBus();
        delay.ms(kScanIntervalMs);
    }
}
