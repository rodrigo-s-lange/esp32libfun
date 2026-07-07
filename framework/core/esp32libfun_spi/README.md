# esp32libfun_spi

Thin SPI master wrapper for `esp32libfun`.

This module keeps ESP-IDF SPI master setup short while preserving explicit bus
and device registration.

## Scope

- initialize one SPI master bus
- register and remove slave devices
- run raw transfers
- write command or register bytes
- read simple register values

## Enable It

Enable the module in Kconfig or `sdkconfig.defaults`:

```text
CONFIG_ESP32LIBFUN_SPI=y
```

Typical dependencies for examples:

```text
CONFIG_ESP32LIBFUN_SERIAL=y
CONFIG_ESP32LIBFUN_DELAY=y
CONFIG_ESP32LIBFUN_AT=y
```

## Public API

- `spi.begin(sclk, mosi, miso, port, max_transfer_sz)`: starts one SPI master bus or acquires a compatible existing bus. Use `mosi=-1` for read-only devices.
- `spi.end(port)`: releases one master bus reference.
- `spi.ready(port)`: reports whether the bus is active.
- `spi.has(cs_pin, port)`: reports whether one device is registered.
- `spi.add(cs_pin, clock_hz, mode, port, queue_size, flags)`: registers one slave device.
- `spi.remove(cs_pin, port)`: removes one registered device.
- `spi.transfer(cs_pin, tx_data, rx_data, len, port)`: runs one generic transfer.
- `spi.write(cs_pin, data, len, port)`: writes raw bytes.
- `spi.read(cs_pin, data, len, port)`: reads raw bytes while clocking out zeros.
- `spi.cmd(cs_pin, value, port)`: writes one command byte.
- `spi.regWrite(cs_pin, reg, data, len, port)`: writes one register plus payload.
- `spi.regWrite8(cs_pin, reg, value, port)`: writes one byte register value.
- `spi.regRead(cs_pin, reg, data, len, port)`: reads one register payload.
- `spi.regRead8(cs_pin, reg, value, port)`: reads one byte register value.
- `spi.at(true)`: registers the optional SPI AT sidecar.

## Frequency Helpers

- `SPI_SLOW`: `1000000`
- `SPI_FAST`: `10000000`
- `SPI_DISPLAY`: `40000000`

## Notes

- this wrapper is master-only
- `mosi=-1` is valid for read-only devices such as simple ADCs and thermocouple front-ends
- generic SPI bus scan does not exist because there is no shared address phase like I2C
- device registration is explicit by CS pin
- this sidecar is aimed at device bring-up, register reads and quick bench diagnostics

## AT Integration

When `CONFIG_ESP32LIBFUN_AT=y`, `spi.at(true)` registers:

- `AT+SPIBUS=<sclk>,<mosi>[,<miso>[,<port>[,<max_transfer>]]]`
- `AT+SPIBUS?<port>`
- `AT+SPIBUSOFF=<port>`
- `AT+SPIADD=<cs>,<clock>[,<mode>[,<port>[,<queue_size>]]]`
- `AT+SPIDEV?<cs>[,<port>]`
- `AT+SPIREMOVE=<cs>[,<port>]`
- `AT+SPICMD=<cs>,<value>[,<port>]`
- `AT+SPIWRITE=<cs>,<hex>[,<port>]`
- `AT+SPIREAD=<cs>,<len>[,<port>]`
- `AT+SPITXRX=<cs>,<hex>[,<port>]`
- `AT+SPIREGWRITE=<cs>,<reg>,<hex>[,<port>]`
- `AT+SPIREGREAD=<cs>,<reg>[,<len>[,<port>]]`

Hex payload notes:

- use plain hex bytes such as `9F`, `AA5501` or `"AA 55 01"`
- quotes are optional and only help when you want spaces in the payload
- read commands answer with `SPIRX=` or `SPIREGRX=` followed by contiguous uppercase hex

## Usage

```cpp
#include "esp32libfun_serial.hpp"
#include "esp32libfun_at.hpp"
#include "esp32libfun_spi.hpp"

static constexpr int kSclkPin = 12;
static constexpr int kMosiPin = 11;
static constexpr int kMisoPin = 13;
static constexpr int kCsPin = 10;

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(serial.init());
    ESP_ERROR_CHECK(at.init());
    ESP_ERROR_CHECK(at.start());

    ESP_ERROR_CHECK(spi.begin(kSclkPin, kMosiPin, kMisoPin));
    ESP_ERROR_CHECK(spi.add(kCsPin, SPI_SLOW));
    ESP_ERROR_CHECK(spi.at(true));
}
```

## Example

- `examples/basic/spi_txrx`

## AT Session Example

```text
AT+SPIBUS=12,11,13
AT+SPIBUS=4,-1,6
AT+SPIADD=10,1000000
AT+SPICMD=10,0x9F
AT+SPITXRX=10,"9F000000"
AT+SPIREGREAD=10,0x0F,1
AT+SPIREGWRITE=10,0x20,"57"
```
