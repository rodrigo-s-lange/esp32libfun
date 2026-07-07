# esp32libfun_w5500

Thin W5500 Ethernet wrapper for `esp32libfun`.

This module stays close to ESP-IDF:

- dedicated SPI bus owned by the W5500 module
- official `esp_eth` driver and official `espressif/w5500` MAC/PHY
- explicit `begin()` / `start()` / `stop()` / `end()`
- DHCP, link state, hostname, MAC, and IPv4 getters with short APIs

## Scope

- initialize one W5500 on one dedicated SPI host
- support interrupt mode through `INT` or polling fallback
- support optional hardware reset GPIO
- expose link state and IPv4 information
- support DHCP by default
- support static IPv4 when `ip()`, `gateway()`, and `subnet()` are configured together

This module does not try to become a generic socket API or a board pin map.

## Lifecycle

This library follows one explicit managed lifecycle:

- `begin(...)` allocates the dedicated SPI bus and installs the Ethernet backend
- `start()` starts the Ethernet state machine and DHCP flow
- `stop()` stops the Ethernet state machine
- `end()` stops Ethernet, destroys the netif and driver, and frees the SPI bus

## Dependencies

This component depends on the official ESP-IDF Ethernet stack and the managed
`espressif/w5500` component declared in `idf_component.yml`.

Typical app examples also use:

```text
framework/core/esp32libfun_serial
framework/core/esp32libfun_at
framework/core/esp32libfun_delay
```

## Public API

- `w5500.begin(miso, mosi, sclk, cs, int_pin, rst_pin, host, clock_hz, queue_size, poll_period_ms)`: initializes the W5500 backend on one dedicated SPI bus.
- `w5500.start()`: starts the Ethernet state machine.
- `w5500.stop()`: stops the Ethernet state machine.
- `w5500.end()`: releases the backend and dedicated SPI bus.
- `w5500.ready()`: reports whether the backend is initialized.
- `w5500.started()`: reports whether the Ethernet state machine is running.
- `w5500.connected()`: reports whether the link is up.
- `w5500.hasIp()`: reports whether one IPv4 address is available.
- `w5500.waitConnected(timeout_ms)`: waits for the link-up event.
- `w5500.ip("...")`: sets the static IPv4 address. Use together with `gateway()` and `subnet()`.
- `w5500.gateway("...")`: sets the static gateway.
- `w5500.subnet("...")`: sets the static subnet mask.
- `w5500.renew()`: restarts DHCP.
- `w5500.hostname("...")`: sets the Ethernet hostname.
- `w5500.mac(mac)`: copies the current MAC to `mac[6]`.
- `w5500.localIP()`: returns the current local IPv4 string.
- `w5500.hostname()`: returns the configured hostname string.
- `w5500.gateway()`: returns the current gateway IPv4 string.
- `w5500.subnet()`: returns the current subnet mask string.
- `w5500.misoPin()`, `w5500.mosiPin()`, `w5500.sclkPin()`, `w5500.csPin()`: return the active SPI pin mapping.
- `w5500.intPin()`, `w5500.rstPin()`: return the optional interrupt and reset pins.
- `w5500.host()`: returns the SPI host in use.
- `w5500.clockHz()`: returns the SPI clock in hertz.
- `w5500.queueSize()`: returns the configured SPI queue size.
- `w5500.pollPeriodMs()`: returns the polling period used when `INT=-1`.
- `w5500.at(true)`: registers the optional W5500 AT sidecar.
- `w5500.atEnabled()`: reports whether the W5500 AT sidecar is active.

## Function Notes

- `begin(...)`: validates pins, owns the dedicated SPI bus, installs the official `esp_eth` backend, and prepares the netif.
- `start()`: starts link management and DHCP or static IPv4 operation.
- `stop()`: stops the Ethernet state machine but keeps the backend allocated.
- `end()`: fully tears down Ethernet and frees the dedicated SPI bus.
- `ip()`, `gateway()`, `subnet()`: define one full static IPv4 configuration. Use the three together.
- `renew()`: restarts DHCP on the active netif. Use only in DHCP mode.
- `hostname()`: sets or clears the Ethernet hostname. The cleanest path is before `begin()`.
- `mac()`: copies the effective MAC address chosen for the W5500 netif.
- `at(true)`: exposes runtime bring-up and diagnostics commands while preserving the explicit lifecycle.

## Clock Notes

- `40 MHz` is the validated default and the safest baseline
- `60 MHz` was previously validated on ESP32-S3 bring-up with real DHCP traffic
- `70-80 MHz` is hardware-dependent and should be treated as experimental

If bring-up fails very early with `reset timeout`, reduce the SPI clock first.
W5500 stability depends heavily on jumper length, breadboard quality, power
integrity, and module layout.

## Wiring Notes

Validated wiring used for current bring-up:

- `MOSI=4`
- `MISO=5`
- `SCLK=6`
- `CS=7`
- `INT=8`
- `RST=9`

If `INT` is not available, pass `-1` and the driver will use polling mode.
If `RST` is not available, pass `-1` and the module will skip hardware reset.

## Enable It

Enable the module in Kconfig or `sdkconfig.defaults`:

```text
CONFIG_ESP32LIBFUN_W5500=y
CONFIG_ESP32LIBFUN_AT=y
```

## Usage

```cpp
#include "esp32libfun.hpp"

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(w5500.hostname("esp32libfun-w5500"));
    ESP_ERROR_CHECK(w5500.begin(5, 4, 6, 7, 8, 9));
    ESP_ERROR_CHECK(w5500.start());
    ESP_ERROR_CHECK(w5500.at(true));

    while (true) {
        serial.println("link=%s ip=%s",
                       w5500.connected() ? "UP" : "DOWN",
                       w5500.localIP());
        delay.s(10);
    }
}
```

## AT Sidecar

Register the sidecar after `esp32libfun_init()`:

```cpp
ESP_ERROR_CHECK(w5500.at(true));
```

Available commands:

- `AT+W5500CFG=<miso>,<mosi>,<sclk>,<cs>[,<int>[,<rst>[,<host>[,<clock_hz>[,<queue_size>[,<poll_ms>]]]]]]`: stores the pending hardware and SPI configuration used by `AT+W5500START`.
- `AT+W5500HOST="name"`: stores one pending hostname. Send an empty string to clear it.
- `AT+W5500IP=DHCP`: selects DHCP for the next `AT+W5500START`.
- `AT+W5500IP="192.168.0.150","192.168.0.1","255.255.255.0"`: stores one full static IPv4 configuration for the next `AT+W5500START`.
- `AT+W5500IP?`: prints current IPv4 information plus the pending DHCP or static mode.
- `AT+W5500?`: prints pending configuration and current runtime state.
- `AT+W5500START[=<timeout_ms>]`: applies the pending hostname and DHCP or static IPv4 settings, starts Ethernet, waits for link, and waits for IPv4. If the backend is still inactive, it also performs the first `begin(...)`.
- `AT+W5500STOP`: stops the Ethernet state machine without freeing the backend.
- `AT+W5500END`: releases the backend and dedicated SPI bus.
- `AT+W5500RENEW`: restarts DHCP. This is intentionally rejected in static mode.
- `AT+W5500PING=<host>[,<count>]`: pings a target through the W5500 link.
- `AT+W5500PINGROUTER[=<count>]`: pings the current gateway.
- `AT+W5500PINGEXTERNAL[=<count>]`: pings `8.8.8.8` to validate external network reachability.

Recommended bring-up:

```text
AT+W5500CFG=5,4,6,7,8,9
AT+W5500IP=DHCP
AT+W5500START=15000
AT+W5500?
AT+W5500PINGROUTER=2
AT+W5500PINGEXTERNAL=2
```

Static IPv4 example:

```text
AT+W5500CFG=5,4,6,7,8,9
AT+W5500HOST="esp32libfun-w5500"
AT+W5500IP="192.168.0.150","192.168.0.1","255.255.255.0"
AT+W5500START=15000
AT+W5500IP?
```

Important notes:

- `AT+W5500IP=...` only stores the pending network mode.
- `AT+W5500START` is the command that reapplies DHCP or static IPv4 with the current backend.
- changing the SPI bus mapping after `begin()` is intentionally not supported yet through the sidecar. Use `AT+W5500CFG` before the first `AT+W5500START`, or restart the app with the new bus wiring.

## Example

- `docs/examples/esp32libfun_w5500_basic.cpp`
- `docs/examples/esp32libfun_w5500_s3_dhcp.cpp`

## Extra References

- `docs/examples/esp32libfun_w5500_basic.cpp`
- `docs/examples/esp32libfun_w5500_s3_dhcp.cpp`
