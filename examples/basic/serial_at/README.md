# Serial AT

Minimal serial output plus the base AT console.

## What It Shows

- `esp32libfun_init()`
- formatted `serial.println()`
- base AT commands: `AT`, `AT+HELP?`, `AT+VER?`

## Run

```powershell
idf.py set-target esp32s3
idf.py -p COM5 flash monitor
```
