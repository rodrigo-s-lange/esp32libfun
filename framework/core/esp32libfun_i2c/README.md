# esp32libfun_i2c

Thin I2C master wrapper for `esp32libfun`.

This module keeps ESP-IDF I2C master setup short while preserving explicit bus
and device registration.

## Scope

- initialize one I2C master bus
- register and remove slave devices
- probe one address
- read and write raw bytes
- read and write simple registers
- reset one active bus

## Enable It

Enable the module in Kconfig or `sdkconfig.defaults`:

```text
CONFIG_ESP32LIBFUN_I2C=y
```

Typical dependencies for examples:

```text
CONFIG_ESP32LIBFUN_SERIAL=y
CONFIG_ESP32LIBFUN_DELAY=y
CONFIG_ESP32LIBFUN_AT=y
```

## Public API

- `i2c.begin(sda, scl, speed_hz, port, internal_pullup)`: starts one master bus or acquires a compatible existing bus.
- `i2c.end(port)`: releases one master bus reference.
- `i2c.ready(port)`: reports whether the bus is active.
- `i2c.has(address, port)`: reports whether one device is registered.
- `i2c.add(address, port, speed_hz, addr_bits)`: registers one slave device.
- `i2c.remove(address, port)`: removes one registered device.
- `i2c.probe(address, port, timeout_ms)`: checks whether one address answers on the bus.
- `i2c.write(address, data, len, port, timeout_ms)`: writes raw bytes.
- `i2c.read(address, data, len, port, timeout_ms)`: reads raw bytes.
- `i2c.writeRead(address, write_data, write_len, read_data, read_len, port, timeout_ms)`: performs one write-then-read transfer.
- `i2c.regWrite(address, reg, data, len, port, timeout_ms)`: writes one register plus payload.
- `i2c.regWrite8(address, reg, value, port, timeout_ms)`: writes one byte register value.
- `i2c.regRead(address, reg, data, len, port, timeout_ms)`: reads one register payload.
- `i2c.regRead8(address, reg, value, port, timeout_ms)`: reads one byte register value.
- `i2c.reset(port)`: resets one active bus.
- `i2c.at(true)`: registers the optional I2C AT sidecar.

## Speed Helpers

- `I2C_STANDARD`: `100000`
- `I2C_FAST`: `400000`
- `I2C_FAST_PLUS`: `1000000`

## Notes

- this wrapper is master-only
- scan is intentionally implemented as repeated `probe()` calls, not as a separate core API
- device registration is explicit so later transfers stay short
- this sidecar is aimed at device bring-up, address discovery and register diagnostics

## AT Integration

When `CONFIG_ESP32LIBFUN_AT=y`, `i2c.at(true)` registers:

- `AT+I2CBUS=<sda>,<scl>[,<speed>[,<port>[,<pullup>]]]`
- `AT+I2CBUS?<port>`
- `AT+I2CBUSOFF=<port>`
- `AT+I2C=SCAN[,<port>]`
- `AT+I2CSCAN[=<port>]`
- `AT+I2CPROBE=<addr>[,<port>]`
- `AT+I2CADD=<addr>[,<port>[,<speed>[,<addr_bits>]]]`
- `AT+I2CDEV?<addr>[,<port>]`
- `AT+I2CREMOVE=<addr>[,<port>]`
- `AT+I2CRESET=<port>`
- `AT+I2CWRITE=<addr>,<hex>[,<port>]`
- `AT+I2CREAD=<addr>,<len>[,<port>]`
- `AT+I2CWRITEREAD=<addr>,<hex>,<len>[,<port>]`
- `AT+I2CREGWRITE=<addr>,<reg>,<hex>[,<port>]`
- `AT+I2CREGREAD=<addr>,<reg>,<len>[,<port>]`

Hex payload notes:

- use plain hex bytes such as `A0`, `00FF12` or `"00 A1 FF"`
- quotes are optional and only help when you want spaces in the payload
- read commands answer with `I2CRX=` or `I2CREGRX=` followed by contiguous uppercase hex

## Usage

```cpp
#include "esp32libfun_serial.hpp"
#include "esp32libfun_at.hpp"
#include "esp32libfun_i2c.hpp"

static constexpr int kSdaPin = 8;
static constexpr int kSclPin = 9;

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(serial.init());
    ESP_ERROR_CHECK(at.init());
    ESP_ERROR_CHECK(at.start());

    ESP_ERROR_CHECK(i2c.begin(kSdaPin, kSclPin, I2C_STANDARD));
    ESP_ERROR_CHECK(i2c.at(true));

    for (uint16_t address = 0x08; address <= 0x77; ++address) {
        if (i2c.probe(address) == ESP_OK) {
            serial.println(C "found 0x%02X", address);
        }
    }
}
```

## Example

- `examples/basic/i2c_scanner`

## AT Session Example

```text
AT+I2CBUS=8,9,100000
AT+I2CSCAN
AT+I2CADD=0x68
AT+I2CREGREAD=0x68,0x75,1
AT+I2CREGWRITE=0x68,0x6B,00
AT+I2CWRITEREAD=0x68,"75",1
```
