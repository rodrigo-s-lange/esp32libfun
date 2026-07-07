# GPIO Blink

Toggles one GPIO and enables the GPIO AT sidecar.

## Default Pin

```text
GPIO43
```

Change `BlinkPin` in `main/main.cpp` if your board uses another LED or test
pin.

## AT Commands

```text
AT+GPIO?43
AT+GPIO=43,1
AT+GPIO=43,0
AT+GPIOTOGGLE=43
```
