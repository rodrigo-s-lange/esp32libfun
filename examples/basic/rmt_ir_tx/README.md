# RMT IR TX

Validates `esp32libfun_rmt` by generating a 38kHz carrier-modulated,
NEC-style byte payload on one GPIO. This is the base TX path an IR
data-link `esp_*` library (or a remote-control emitter) would build on.

## Wiring

For the ESP32-S3 validation setup:

```text
GPIO43 -> oscilloscope probe (GND on board ground)
```

Use another output-capable GPIO if GPIO43 is unavailable on your board.

## What It Tests

- one RMT TX channel with 38kHz / 33% duty carrier modulation
- the bit0/bit1 byte encoder (`rmt.bits()` + `rmt.write()`)
- repeated transmission of a fixed 2-byte payload (`0xA5, 0x3C`, MSB first)

## Validated

Confirmed on real ESP32-S3 hardware with an oscilloscope:

- carrier period measured at 26.32-26.4us (theoretical `1/38000s` = 26.32us)
- mark/space bursts matching the configured NEC-style timing

## Run

```powershell
idf.py set-target esp32s3
idf.py -p COM14 flash monitor
```

Adjust the target and port for your board.

## What To Look For

- carrier bursts at ~38kHz whenever the signal is high, not a continuous
  square wave
- each bit starts with a ~562us mark, followed by a ~562us space (bit `0`)
  or a ~1690us space (bit `1`)
- the whole 2-byte frame repeats every ~500ms
