# esp32libfun_lan8720

Thin LAN8720 RMII Ethernet wrapper for `esp32libfun`.

This module keeps close to ESP-IDF:

- internal ESP EMAC owned by the LAN8720 module
- official `esp_eth` driver plus the external `espressif/lan87xx` PHY component required by ESP-IDF 6.0
- explicit `begin()` / `start()` / `stop()` / `end()`
- DHCP and link state exposed with short getters

## Target Notes

- available only on targets with internal EMAC support
- on ESP-IDF 6.0, the LAN87xx PHY driver is no longer shipped inside ESP-IDF and must come from the component registry
- the current project target `ESP32-S3` does not expose internal EMAC, so this core stays disabled there

## Clock Notes

- `LAN8720_CLK_EXT_IN` expects the PHY or board to provide the RMII reference clock
- `LAN8720_CLK_OUT` uses the SoC EMAC clock output path when the target supports it
- clock selection is configured in code because ESP-IDF 6.0 removed the old RMII clock Kconfig path
- on the classic ESP32, a common LAN8720 design uses `GPIO0` on the PHY `nINT/REFCLKO` line as the RMII clock input
- some boards keep the PHY unpowered until one extra GPIO is driven high, such as `GPIO12`, which also prevents the 50 MHz clock from disturbing `GPIO0` boot strapping

## Enable it

Enable the module in Kconfig or `sdkconfig.defaults` on a supported target:

```text
CONFIG_ESP32LIBFUN_LAN8720=y
```

## Usage

```cpp
#include "esp32libfun.hpp"
#include "esp32libfun_lan8720.hpp"

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(lan8720.hostname("esp32libfun-lan8720"));
    ESP_ERROR_CHECK(lan8720.begin(23, 18, ESP_ETH_PHY_ADDR_AUTO, -1, LAN8720_CLK_EXT_IN, 0, 12));
    ESP_ERROR_CHECK(lan8720.start());

    while (true) {
        serial.println("link=%s ip=%s",
                       lan8720.connected() ? "UP" : "DOWN",
                       lan8720.localIP());
        delay.s(2);
    }
}
```

For the classic ESP32 RMII wiring you described, the common mapping is:

- `clock_gpio = 0` on the LAN8720 `nINT/REFCLKO` pin used as RMII reference clock input
- `clock_enable_pin = 12` when the board uses that GPIO as PHY power control or external clock gate
- `mdio = 18`
- `txd0 = 19`
- `tx_en = 21`
- `txd1 = 22`
- `mdc = 23`
- `rxd0 = 25`
- `rxd1 = 26`
- `crs_dv = 27`

The RMII data pins above follow the classic ESP32 internal EMAC routing. In this wrapper, `begin()` configures the controllable pieces: `MDC`, `MDIO`, PHY address, optional reset, RMII clock source, and the optional board GPIO used as PHY power or clock enable.
