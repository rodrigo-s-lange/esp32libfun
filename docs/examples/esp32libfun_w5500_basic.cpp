#include "esp32libfun.hpp"
#include "esp32libfun_w5500.hpp"

namespace {

constexpr int ethMisoPin = 37;
constexpr int ethMosiPin = 35;
constexpr int ethSclkPin = 36;
constexpr int ethCsPin = 38;
constexpr int ethIntPin = 39;
constexpr int ethRstPin = 40;

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
        W5500::DEFAULT_CLOCK_HZ));
    ESP_ERROR_CHECK(w5500.start());

    serial.println(O "W5500 host=" C "%d" O " clock=" C "%lu",
                   w5500.host(),
                   static_cast<unsigned long>(w5500.clockHz()));

    while (true) {
        serial.println(O "link=" C "%s" O " ip=" C "%s",
                       w5500.connected() ? "UP" : "DOWN",
                       w5500.localIP());
        delay.s(2);
    }
}
