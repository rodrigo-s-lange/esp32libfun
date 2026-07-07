# LEDC Fade

Runs a PWM fade loop and enables the LEDC AT sidecar.

## Default Pin

```text
GPIO43
```

## AT Commands

```text
AT+LEDC?43
AT+LEDC=43,50
AT+LEDCFADE=43,100,1000
AT+LEDCFADE=43,0,1000
```
