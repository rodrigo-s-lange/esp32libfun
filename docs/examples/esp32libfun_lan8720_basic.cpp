#include "esp32libfun.hpp"
#include "esp32libfun_lan8720.hpp"

namespace {

// Classic ESP32 RMII wiring with LAN8720.
// RMII data pins are fixed by the internal EMAC routing:
//   RXD0=25  RXD1=26  CRS_DV=27  TXD0=19  TX_EN=21  TXD1=22
// Only MDC, MDIO, clock source, and PHY power are configured here.
constexpr int ethMdcPin         = 23;
constexpr int ethMdioPin        = 18;
constexpr int ethClockGpio      = 0;   // PHY nINT/REFCLKO fed as RMII ref clock
constexpr int ethClockEnablePin = 12;  // board GPIO that powers the PHY

} // namespace

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(lan8720.hostname("esp32libfun-lan8720"));
    ESP_ERROR_CHECK(lan8720.begin(
        ethMdcPin,
        ethMdioPin,
        LAN8720_PHY_ADDR_AUTO,
        Lan8720::DEFAULT_RESET_PIN,
        LAN8720_CLK_EXT_IN,
        ethClockGpio,
        ethClockEnablePin));
    ESP_ERROR_CHECK(lan8720.start());

    while (true) {
        serial.println(O "link=" C "%s" O " ip=" C "%s",
                       lan8720.connected() ? "UP" : "DOWN",
                       lan8720.localIP());
        delay.s(2);
    }
}
