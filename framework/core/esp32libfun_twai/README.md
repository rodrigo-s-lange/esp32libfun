# esp32libfun_twai

Thin TWAI wrapper for `esp32libfun`.

TWAI is Espressif's CAN-compatible controller. This module owns on-chip node
lifetime, frame TX/RX, basic status, mask filters, loopback, and self-test
bring-up. It does not implement higher-level CANopen, J1939, OBD-II, or device
protocol logic.

## Scope

- create and release one on-chip TWAI node
- transmit classic TWAI frames and optional TWAI FD frames on targets that support FD
- receive frames through an internal FreeRTOS queue
- run loopback/self-test without an external transceiver for bench validation
- read node status and bus error counters
- configure a simple mask acceptance filter
- expose the native `twai_node_handle_t` as an escape hatch

## Enable It

```text
CONFIG_ESP32LIBFUN_TWAI=y
```

## Public API

- `twai.begin(tx, rx, bitrate, tx_queue_depth, loopback, self_test, listen_only)`: creates and enables one node.
- `twai.end(tx)`: disables and releases the node.
- `twai.ready(tx)`: reports whether the TX pin owns a node.
- `twai.node(tx)`: returns the native node handle as `void *`.
- `twai.write(tx, frame, timeout_ms)`: transmits one `TwaiFrame`.
- `twai.write(tx, id, data, len, ext, timeout_ms)`: transmits one classic frame.
- `twai.read(tx, &frame, timeout_ms)`: reads one received frame from the queue.
- `twai.wait(tx, timeout_ms)`: waits until queued transmissions finish.
- `twai.recover(tx)`: starts bus-off recovery.
- `twai.status(tx, &status)`: reads node status and counters.
- `twai.filter(tx, id, mask, ext, filter_id)`: configures one mask filter.

## Notes

- Real bus operation requires a CAN/TWAI transceiver. The ESP32 GPIOs are not a
  differential CAN physical layer.
- `loopback=true` and `self_test=true` are useful for validating the wrapper
  without a transceiver or another node.
- RX is queue based. Call `twai.read()` from task context.
- `TwaiFrame::fd` is accepted only on targets that support TWAI FD.

## Usage

```cpp
#include "esp32libfun.hpp"

constexpr int kTwaiTx = 4;
constexpr int kTwaiRx = 5;

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(twai.begin(kTwaiTx, kTwaiRx, 500000, 5, true, true));

    const uint8_t payload[] = {0x12, 0x34};
    ESP_ERROR_CHECK(twai.write(kTwaiTx, 0x123, payload, sizeof(payload)));

    TwaiFrame frame = {};
    if (twai.read(kTwaiTx, &frame, 1000) == ESP_OK) {
        serial.println(C "TWAI id=0x%lX len=%u", static_cast<unsigned long>(frame.id), static_cast<unsigned>(frame.len));
    }
}
```
