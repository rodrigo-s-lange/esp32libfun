# SPI TX/RX

Initializes one SPI master bus and repeatedly transfers a short frame to one
device.

## Default Pins

```text
SCLK = GPIO4
CS   = GPIO5
MOSI = GPIO6
MISO = GPIO7
```

The example expects a SPI mode 0 device connected to `CS`. With the simple
echo-test device used during validation, the first response is zero and later
responses echo the previous frame.

## AT Commands

```text
AT+SPIBUS?1
AT+SPIDEV?5
AT+SPITXRX=5,A55A00010203C33C
```
