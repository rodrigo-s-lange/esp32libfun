# esp_si7021

Si7021 temperature and humidity sensor library for `esp32libfun`.

What it demonstrates:

- an `esp_*` library built on top of `esp32libfun_i2c`
- `init()/start()/stop()/end()` when manual and managed modes both make sense
- device attachment to an already initialized I2C bus
- manual polling with `read()` / `loop()`
- optional callback after each successful acquisition

Usage model:

1. Initialize the shared I2C bus with `i2c.begin(...)`
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
    serial.println(C "Temp: " O "%.2f C" C "  RH: " O "%.2f %%",
                   sensor.temperature(),
                   sensor.humidity());
}

extern "C" void app_main(void)
{
    esp32libfun_init();

    i2c.begin(kSda, kScl, I2C_FAST);
    si7021.init();
    si7021.onRead(onRead);
    si7021.start();

    while (true) {
        delay.s(1);
    }
}
```
