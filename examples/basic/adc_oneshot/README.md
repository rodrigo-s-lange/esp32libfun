# ADC Oneshot

Validates `esp32libfun_adc` by reading one ADC-capable GPIO with the ESP-IDF
oneshot driver.

## Wiring

For ESP32-S3 bring-up, connect a safe analog voltage to GPIO4, or leave it
floating just to verify the API path. Keep the voltage inside the valid range
for the target and selected attenuation.

## What It Tests

- GPIO-to-ADC unit/channel resolution
- raw ADC reads
- calibrated millivolt reads when calibration is available

## Run

```powershell
idf.py set-target esp32s3
idf.py -p COMx flash monitor
```
