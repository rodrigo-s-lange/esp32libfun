# esp_bmp280

BMP280 temperature and pressure sensor library for `esp32libfun`.

What fits the framework style here:

- uses `esp32libfun_i2c` instead of owning the whole bus
- keeps `init()` for manual mode and `start()` for the optional managed task
- validates the sensor identity early through the BMP280 chip ID
- exposes short state getters like `ready()`, `started()`, `address()`, `port()`, and `intervalMs()`

I2C address:

| SDO pin | Address |
|---------|---------|
| GND     | `0x76`  |
| VCC     | `0x77`  |

What `init()` does:

1. Checks that `i2c.begin()` was already called by the application
2. Probes the selected I2C address
3. Registers the device through `i2c.add()`
4. Verifies the BMP280 chip ID at register `0xD0`
5. Resets the device and waits for startup
6. Reads the factory calibration block from `0x88`
7. Leaves the device ready for forced-mode reads

Minimal example:

```cpp
#include "esp32libfun.hpp"
#include "esp_bmp280.hpp"

constexpr int kSda = 4;
constexpr int kScl = 5;

static void onRead(Bmp280 &sensor)
{
    serial.println(C "BMP280  temp: " O "%.2f C" C "  press: " O "%.2f hPa",
                   sensor.temperature(),
                   sensor.pressure());
}

extern "C" void app_main(void)
{
    esp32libfun_init();

    i2c.begin(kSda, kScl, I2C_FAST);
    bmp280.init();
    bmp280.intervalMs(1000);
    bmp280.onRead(onRead);
    bmp280.start();

    while (true) {
        delay.s(1);
    }
}
```

Examples:

- `docs/examples/esp_bmp280_manual.cpp`
- `docs/examples/esp_bmp280_managed.cpp`
