# esp32libfun_rmt

Thin RMT (Remote Control Transceiver) wrapper for `esp32libfun`.

This module owns channel lifecycle, carrier modulation, and the two generic
ways to move data through an RMT channel: byte-oriented (bit0/bit1 timing) and
raw symbols. It does not implement any protocol: IR framing (NEC, etc.),
WS2812/addressable LED timing, and similar logic belong in an `esp_*` library
built on top, using this module for the channel and `channel(pin)` to reach
the raw ESP-IDF handle when a protocol needs a custom encoder.

## Scope

- create and release one TX or one RX channel per pin
- enable/disable carrier modulation (TX) or demodulation (RX), e.g. 38kHz for IR
- transmit byte payloads through a configurable bit0/bit1 encoder
- transmit raw `rmt_symbol_t` arrays directly
- receive raw symbols through one non-blocking job with a task-context callback
- escape hatch to the native `rmt_channel_handle_t` for advanced encoders

This module does not implement IR protocols, LED strip timing, DShot, or any
other framing on top of RMT.

## Enable It

Enable the module in Kconfig or `sdkconfig.defaults`:

```text
CONFIG_ESP32LIBFUN_RMT=y
```

## Public API

- `rmt.beginTx(pin, resolution_hz, mem_block_symbols, trans_queue_depth)`: creates and enables one TX channel.
- `rmt.beginRx(pin, resolution_hz, mem_block_symbols)`: creates and enables one RX channel.
- `rmt.end(pin)`: disables and releases the channel.
- `rmt.ready(pin)`: reports whether the pin already owns a channel.
- `rmt.channel(pin)`: returns the native channel handle as `void *` (cast to `rmt_channel_handle_t` after including `driver/rmt_types.h`), or `nullptr`.
- `rmt.carrier(pin, freq_hz, duty, active_low)`: applies carrier modulation/demodulation.
- `rmt.carrierOff(pin)`: disables carrier modulation/demodulation.
- `rmt.bits(pin, bit0, bit1, msb_first)`: configures the bit0/bit1 timing used by `write()`.
- `rmt.write(pin, data, len, wait)`: encodes and transmits a byte payload.
- `rmt.writeSymbols(pin, symbols, count, wait)`: transmits raw symbols directly.
- `rmt.wait(pin, timeout_ms)`: blocks until queued transmissions finish.
- `rmt.receive(pin, buffer, buffer_symbols, signal_range_min_ns, signal_range_max_ns, callback, user_ctx)`: starts one receive job; `callback` fires once in task context.

## Notes

- `rmt_symbol_t` is this module's own struct, laid out bit-for-bit like the
  ESP-IDF `rmt_symbol_word_t` (same `duration0`/`level0`/`duration1`/`level1`
  fields). It is not the ESP-IDF type itself; the header stays dependency-free
  on purpose so the framework aggregator can include it without needing
  `esp_driver_rmt`'s include path.
- `write()` requires `bits()` to be configured first on the same pin; it
  returns `ESP_ERR_INVALID_STATE` otherwise.
- `receive()` is one-shot: call it again (typically from inside `callback`) to
  keep listening.
- receive callbacks run in task context, not directly in ISR context, matching
  `esp32libfun_pcnt`.
- `duty` in `carrier()` is a fraction (`0.33f` for 33%), matching the ESP-IDF
  RMT examples, not a 0-100 percentage.
- `MAX_CHANNELS` is a practical ceiling for this wrapper's internal slot table,
  not a hardware guarantee. `beginTx()`/`beginRx()` return `ESP_ERR_NO_MEM`
  when this wrapper has no free internal slot left; the underlying driver may
  return another `esp_err_t` if hardware channels are exhausted.
- When `wait=false`, keep the payload buffer alive until `wait(pin)` reports
  that the queued transmission has completed.

## Usage

```cpp
#include "esp32libfun.hpp"

static constexpr int kIrTxPin = 4;

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(rmt.beginTx(kIrTxPin));
    ESP_ERROR_CHECK(rmt.carrier(kIrTxPin, 38000, 0.33f));

    // NEC-style bit timing: 562.5us mark, 562.5/1687.5us space
    rmt_symbol_t bit0 = {.duration0 = 560, .level0 = 1, .duration1 = 560, .level1 = 0};
    rmt_symbol_t bit1 = {.duration0 = 560, .level0 = 1, .duration1 = 1690, .level1 = 0};
    ESP_ERROR_CHECK(rmt.bits(kIrTxPin, bit0, bit1));

    const uint8_t payload[2] = {0x04, 0x30};
    while (true) {
        ESP_ERROR_CHECK(rmt.write(kIrTxPin, payload, sizeof(payload)));
        delay.s(1);
    }
}
```
