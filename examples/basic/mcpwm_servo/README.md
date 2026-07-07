# MCPWM Servo

Generates a servo-style MCPWM pulse sweep for scope and servo validation.

## Default Pin

```text
GPIO43
```

## Signal

- frequency: 50 Hz
- period: 20000 us
- pulse widths: 1000 us, 1500 us, 2000 us
- resolution: 1 MHz, so one MCPWM tick maps to one microsecond

## Expected Scope Readings

```text
1000 us high, 19000 us low
1500 us high, 18500 us low
2000 us high, 18000 us low
```

## Run

```powershell
idf.py set-target esp32s3
idf.py -p COM5 flash monitor
```
