# GPTimer Tick

Validates `esp32libfun_gptimer` by running a 1MHz timer and printing a
task-context alarm every second.

## What It Tests

- timer creation
- raw elapsed microsecond reads
- periodic alarm callback delivered outside ISR context

## Run

```powershell
idf.py set-target esp32s3
idf.py -p COMx flash monitor
```
