# esp_si7021

Si7021 temperature and humidity sensor library for `esp32libfun`.

What fits the framework style here:

- uses `esp32libfun_i2c` instead of owning the whole bus
- keeps `init()` for manual mode and `start()` for the optional managed task
- exposes short state getters like `ready()`, `started()`, `address()`, `port()`, and `intervalMs()`
- keeps the common path obvious: `i2c.begin(...)`, `si7021.init()`, then `read()`/`loop()` or `start()`

Usage model:

1. Start the shared I2C bus with `i2c.begin(...)`
2. Attach the sensor with `si7021.init(...)`
3. Use either:
   - `si7021.read()` or `si7021.loop()` in manual mode
   - `si7021.start()` for periodic background reads

Minimal example:

```cpp
#include "esp32libfun.hpp"
#include "esp_si7021.hpp"

constexpr int kSda = 4;
constexpr int kScl = 5;

static void onRead(Si7021 &sensor)
{
    serial.println(C "Si7021  temp: " O "%.2f C" C "  hum: " O "%.2f %%",
                   sensor.temperature(),
                   sensor.humidity());
}

extern "C" void app_main(void)
{
    esp32libfun_init();

    i2c.begin(kSda, kScl, I2C_FAST);
    si7021.init();
    si7021.intervalMs(1000);
    si7021.onRead(onRead);
    si7021.start();

    while (true) {
        delay.s(1);
    }
}
```

Examples:

- `docs/examples/esp_si7021_manual.cpp`
- `docs/examples/esp_si7021_managed.cpp`
