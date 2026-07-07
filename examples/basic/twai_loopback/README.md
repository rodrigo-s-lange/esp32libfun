# TWAI Loopback

Validates `esp32libfun_twai` by using TWAI loopback and self-test mode. This
does not require a CAN transceiver or another node.

## Wiring

No bus wiring is required for loopback/self-test. The example still assigns
GPIO4 as TX and GPIO5 as RX so the same code shape can be adapted to a real
transceiver later.

For a real CAN bus, connect TX/RX to a compatible CAN/TWAI transceiver and use
proper bus termination.

## What It Tests

- TWAI node creation
- loopback/self-test transmission without ACK
- RX queue delivery through `twai.read()`
- status readout

## Run

```powershell
idf.py set-target esp32s3
idf.py -p COMx flash monitor
```
