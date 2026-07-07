# esp32libfun_wifi_sta

Thin Wi-Fi station wrapper for `esp32libfun`.

This module keeps common ESP-IDF STA setup short and readable without trying to
hide the underlying Wi-Fi model.

## Scope

- initialize the ESP-IDF Wi-Fi station stack on first use
- start or reconfigure station connection with `begin()`
- optional static IPv4 settings before connect
- optional hostname before connect
- disconnect and clean helpers
- connection-state polling and wait helper
- last observed local IPv4 string

This module does not implement provisioning, captive portal flows, credential
storage UX, or automatic reconnect policy beyond what ESP-IDF already does.

## Enable It

Enable the module in Kconfig or `sdkconfig.defaults`:

```text
CONFIG_ESP32LIBFUN_WIFI_STA=y
```

Typical dependencies for examples:

```text
CONFIG_ESP32LIBFUN_SERIAL=y
CONFIG_ESP32LIBFUN_DELAY=y
```

## Public API

- `wifi.begin(ssid, password)`: starts or reconfigures the STA connection flow.
- `wifi.clean()`: clears the current STA configuration and local wrapper state.
- `wifi.disconnect()`: disconnects the current station session without wiping config.
- `wifi.isConnected()`: reports whether the connection bit is currently set.
- `wifi.waitConnected(timeout_ms)`: waits until the station is connected or timeout expires.
- `wifi.ip(ip)`: sets a static IPv4 address to apply before `begin()`.
- `wifi.gateway(gateway)`: sets a static IPv4 gateway to apply before `begin()`.
- `wifi.subnet(subnet)`: sets a static IPv4 subnet mask to apply before `begin()`.
- `wifi.hostname(hostname)`: sets the hostname to apply before `begin()`.
- `wifi.localIP()`: returns the last IPv4 address observed by the event handler.
- `wifi.gatewayIP()`: returns the current IPv4 gateway reported by the STA netif.
- `wifi.subnetMask()`: returns the current IPv4 subnet mask reported by the STA netif.
- `wifi.dns1()`: returns the current primary DNS server reported by the STA netif.
- `wifi.dns2()`: returns the current secondary DNS server reported by the STA netif.
- `wifi.at(true)`: registers the optional Wi-Fi STA AT sidecar.

## Connection Notes

- call `wifi.hostname()`, `wifi.ip()`, `wifi.gateway()`, and `wifi.subnet()` before `wifi.begin()`
- static IPv4 requires all three values together: IP, gateway, and subnet
- leaving those fields unset keeps the normal DHCP path
- `wifi.begin()` intentionally stays close to ESP-IDF and is the main public entry point
- `wifi.localIP()` returns `"0.0.0.0"` until the station receives an address

## AT Integration

When the component is built together with `esp32libfun_at`, `wifi.at(true)` registers:

- `AT+WIFICRED="ssid"[,"password"]`
- `AT+WIFIHOST="hostname"`
- `AT+WIFIIP=DHCP`
- `AT+WIFIIP=<ip>,<gateway>,<subnet>`
- `AT+WIFIIP?`
- `AT+WIFI?`
- `AT+WIFICONNECT[=<timeout_ms>]`
- `AT+WIFIRECONNECT[=<timeout_ms>]`
- `AT+WIFIDISCONNECT`
- `AT+WIFISAVE`
- `AT+WIFILOAD`
- `AT+WIFIFORGET`
- `AT+WIFIPING=<host>[,<count>]`
- `AT+WIFIPINGROUTER[=<count>]`

This sidecar is intended for bring-up and field diagnostics:

- keep credentials in RAM while iterating
- save or load a known-good profile from NVS
- switch between DHCP and static IPv4
- connect, reconnect, disconnect, inspect status and ping a router or external host
- inspect runtime gateway, subnet mask and DNS servers reported by the STA netif

## Usage

```cpp
#include "esp32libfun_at.hpp"
#include "esp32libfun_serial.hpp"
#include "esp32libfun_wifi_sta.hpp"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(serial.init());
    ESP_ERROR_CHECK(at.init());
    ESP_ERROR_CHECK(at.start());
    ESP_ERROR_CHECK(wifi.at(true));

    serial.println(C "AT+WIFICRED=\"ssid\",\"password\"");
    serial.println(C "AT+WIFIHOST=\"esp32libfun\"");
    serial.println(C "AT+WIFICONNECT");
    serial.println(C "AT+WIFI?");
}
```

## Static IP Example

```cpp
#include "esp32libfun.hpp"

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(wifi.hostname("esp32libfun-static"));
    ESP_ERROR_CHECK(wifi.ip("192.168.1.50"));
    ESP_ERROR_CHECK(wifi.gateway("192.168.1.1"));
    ESP_ERROR_CHECK(wifi.subnet("255.255.255.0"));
    ESP_ERROR_CHECK(wifi.begin("YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD"));
}
```

## AT Session Example

```text
AT+WIFICRED="LabWiFi","secret123"
AT+WIFIHOST="esp32libfun"
AT+WIFIIP=DHCP
AT+WIFICONNECT
AT+WIFI?
AT+WIFIPINGROUTER=2
AT+WIFIPING="192.168.1.1",2
AT+WIFIPING="8.8.8.8",2
AT+WIFISAVE
```

## Example

- Wi-Fi STA buildable example: pending
