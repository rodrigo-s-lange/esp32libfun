# Basic Examples

Buildable ESP-IDF examples for the core `esp32libfun_*` modules.

Each directory is an independent ESP-IDF project that uses the framework core
from this repository through `EXTRA_COMPONENT_DIRS`.

## Examples

- `serial_at`: serial output plus the base AT console
- `gpio_blink`: GPIO output and GPIO AT commands
- `ledc_fade`: PWM output, fade loop, and LEDC AT commands
- `i2c_scanner`: I2C bus bring-up and address scan
- `spi_txrx`: SPI bus/device setup and transfer helper
- `pcnt_ledc_counter`: LEDC-generated pulses counted by PCNT
- `mcpwm_servo`: servo-style MCPWM pulse-width validation

## Run

From one example directory:

```powershell
idf.py set-target esp32s3
idf.py -p COM5 flash monitor
```

Adjust target, port, and pins for your hardware.
