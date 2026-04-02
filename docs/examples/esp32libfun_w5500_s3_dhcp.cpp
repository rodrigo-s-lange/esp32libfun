#include "esp32libfun.hpp"
#include "esp32libfun_w5500.hpp"

namespace {

constexpr int ethMisoPin = 37;
constexpr int ethMosiPin = 35;
constexpr int ethSclkPin = 36;
constexpr int ethCsPin = 38;
constexpr int ethIntPin = 39;
constexpr int ethRstPin = 40;

// 60 MHz was validated on this ESP32-S3 + W5500 setup.
constexpr uint32_t ethClockHz = 60000000;

} // namespace

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(w5500.hostname("esp32libfun-w5500"));
    ESP_ERROR_CHECK(w5500.begin(
        ethMisoPin,
        ethMosiPin,
        ethSclkPin,
        ethCsPin,
        ethIntPin,
        ethRstPin,
        W5500::DEFAULT_HOST,
        ethClockHz));
    ESP_ERROR_CHECK(w5500.start());

    serial.println(O "W5500 test host=" C "%d" O " clock=" C "%lu",
                   w5500.host(),
                   static_cast<unsigned long>(w5500.clockHz()));

    while (true) {
        uint8_t mac[6] = {};
        if (w5500.mac(mac) == ESP_OK) {
            serial.println(O "link=" C "%s" O " ip=" C "%s" O " mac=" C "%02X:%02X:%02X:%02X:%02X:%02X",
                           w5500.connected() ? "UP" : "DOWN",
                           w5500.localIP(),
                           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        } else {
            serial.println(O "link=" C "%s" O " ip=" C "%s",
                           w5500.connected() ? "UP" : "DOWN",
                           w5500.localIP());
        }
        delay.s(2);
    }
}
