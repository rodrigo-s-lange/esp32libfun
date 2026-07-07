#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

namespace esp32libfun {

/// One RMT symbol: two (duration, level) pairs, laid out bit-for-bit like the
/// ESP-IDF HAL's `rmt_symbol_word_t`. Defined locally instead of including
/// "driver/rmt_types.h" so this header stays dependency-free like every other
/// core module header; `esp32libfun_rmt.cpp` converts to/from the real type
/// at the API boundary (see the `static_assert` there).
struct RmtSymbol {
    uint16_t duration0 : 15;
    uint16_t level0 : 1;
    uint16_t duration1 : 15;
    uint16_t level1 : 1;
};

using rmt_symbol_t = RmtSymbol;

/// Callback fired in task context when a receive job started with
/// `Rmt::receive()` completes.
///
/// @param pin RX pin the job was started on.
/// @param symbols Buffer originally passed to `Rmt::receive()`, now filled in.
/// @param count Number of symbols actually received.
/// @param user_ctx Opaque pointer passed through from `Rmt::receive()`.
using rmt_rx_callback_t = void (*)(int pin, const rmt_symbol_t *symbols, size_t count, void *user_ctx);

/// Thin wrapper around the ESP-IDF RMT TX/RX driver, keyed by GPIO pin.
///
/// This module only owns channel lifecycle, carrier modulation, and the two
/// generic ways to move data through a channel (byte-oriented and raw
/// symbols). Protocol logic such as IR NEC framing or WS2812 timing belongs
/// in an `esp_*` library built on top; use `channel()` to reach the raw
/// driver handle when that library needs the native driver directly. Cast
/// the returned `void *` to `rmt_channel_handle_t` after including
/// "driver/rmt_types.h" in the caller's own file.
class Rmt {
public:
    static constexpr size_t MAX_CHANNELS = 8;
    static constexpr uint32_t DEFAULT_RESOLUTION_HZ = 1000000;
    static constexpr size_t DEFAULT_MEM_BLOCK_SYMBOLS = 64;
    static constexpr size_t DEFAULT_TRANS_QUEUE_DEPTH = 4;

    /// Creates and enables one TX channel on the selected pin.
    ///
    /// @param pin Output-capable GPIO to use as the TX channel.
    /// @param resolution_hz Tick resolution for this channel, in Hz (1 tick = 1/resolution_hz seconds).
    /// @param mem_block_symbols RMT memory block size for this channel, in number of symbols.
    /// @param trans_queue_depth Number of queued transmissions the channel accepts before blocking.
    /// @return `ESP_OK` on success, `ESP_ERR_INVALID_ARG` for an invalid pin,
    ///         `ESP_ERR_INVALID_STATE` if the pin already owns a channel, or
    ///         another `esp_err_t` from the underlying driver.
    esp_err_t beginTx(int pin,
                      uint32_t resolution_hz = DEFAULT_RESOLUTION_HZ,
                      size_t mem_block_symbols = DEFAULT_MEM_BLOCK_SYMBOLS,
                      size_t trans_queue_depth = DEFAULT_TRANS_QUEUE_DEPTH) const;
    /// Creates and enables one RX channel on the selected pin.
    ///
    /// @param pin GPIO to use as the RX channel.
    /// @param resolution_hz Tick resolution for this channel, in Hz.
    /// @param mem_block_symbols RMT memory block size for this channel, in number of symbols.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t beginRx(int pin,
                      uint32_t resolution_hz = DEFAULT_RESOLUTION_HZ,
                      size_t mem_block_symbols = DEFAULT_MEM_BLOCK_SYMBOLS) const;
    /// Disables and releases the channel on the selected pin.
    ///
    /// @param pin Pin whose channel should be disabled and released.
    /// @return `ESP_OK` on success, or `ESP_ERR_NOT_FOUND` if the pin owns no channel.
    esp_err_t end(int pin) const;
    /// Returns true when the selected pin already owns a channel.
    ///
    /// @param pin Pin to check.
    /// @return true when the pin already owns a channel.
    [[nodiscard]] bool ready(int pin) const;
    /// Returns the raw `rmt_channel_handle_t` for the selected pin, or `nullptr` if none.
    ///
    /// @param pin Pin whose native driver handle should be returned.
    /// @return The channel handle cast to `void *`, or `nullptr` if the pin owns no channel.
    [[nodiscard]] void *channel(int pin) const;

    /// Enables carrier modulation (TX) or demodulation (RX) on the selected channel.
    ///
    /// @param pin Pin whose channel should get carrier modulation/demodulation.
    /// @param freq_hz Carrier frequency in Hz (e.g. `38000` for IR).
    /// @param duty Carrier duty cycle as a fraction from `0.0` to `1.0`, not a percentage.
    /// @param active_low When true, inverts which logical level carries the carrier.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t carrier(int pin, uint32_t freq_hz, float duty = 0.33f, bool active_low = false) const;
    /// Disables carrier modulation/demodulation on the selected channel.
    ///
    /// @param pin Pin whose carrier modulation/demodulation should be disabled.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t carrierOff(int pin) const;

    /// Configures the bit0/bit1 timing later used by `write()` on the selected TX channel.
    ///
    /// @param pin TX pin whose byte encoder should be configured.
    /// @param bit0 Symbol timing used to encode a `0` bit.
    /// @param bit1 Symbol timing used to encode a `1` bit.
    /// @param msb_first When true, encodes each byte most-significant-bit first.
    /// @return `ESP_OK` on success, `ESP_ERR_INVALID_STATE` if the pin is not a TX
    ///         channel, or another `esp_err_t` from the underlying driver.
    esp_err_t bits(int pin, rmt_symbol_t bit0, rmt_symbol_t bit1, bool msb_first = true) const;
    /// Encodes and transmits a byte payload using the timing set by `bits()`.
    ///
    /// @param pin TX pin to transmit on; must already have `bits()` configured.
    /// @param data Byte payload to encode and transmit.
    /// @param len Number of bytes in `data`.
    /// @param wait When true, blocks until the transmission finishes before returning.
    /// @return `ESP_OK` on success, `ESP_ERR_INVALID_STATE` if `bits()` was not
    ///         configured, or another `esp_err_t` from the underlying driver.
    esp_err_t write(int pin, const uint8_t *data, size_t len, bool wait = true) const;
    /// Transmits raw RMT symbols directly, bypassing the bit0/bit1 encoder.
    ///
    /// @param pin TX pin to transmit on.
    /// @param symbols Raw RMT symbols to transmit as-is.
    /// @param count Number of symbols in `symbols`.
    /// @param wait When true, blocks until the transmission finishes before returning.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t writeSymbols(int pin, const rmt_symbol_t *symbols, size_t count, bool wait = true) const;
    /// Blocks until all queued transmissions on the selected channel finish.
    ///
    /// @param pin TX pin to wait on.
    /// @param timeout_ms Maximum time to wait, in milliseconds; `-1` waits forever.
    /// @return `ESP_OK` once queued transmissions finish, or `ESP_ERR_TIMEOUT`.
    esp_err_t wait(int pin, int timeout_ms = -1) const;

    /// Starts one non-blocking receive job. `callback` fires once in task context when
    /// `signal_range_max_ns` of silence is seen or `buffer_symbols` fills up. Call
    /// `receive()` again from the callback to keep listening.
    ///
    /// @param pin RX pin to listen on.
    /// @param buffer Caller-owned buffer that receives the raw symbols; must stay
    ///        valid until `callback` fires.
    /// @param buffer_symbols Capacity of `buffer`, in number of symbols.
    /// @param signal_range_min_ns Pulses shorter than this are treated as glitches and ignored.
    /// @param signal_range_max_ns Receiving stops once one level has held longer than this, in nanoseconds.
    /// @param callback Function invoked in task context once the job completes.
    /// @param user_ctx Opaque pointer passed through to `callback`.
    /// @return `ESP_OK` once the job is queued, or an `esp_err_t` describing the failure.
    esp_err_t receive(int pin, rmt_symbol_t *buffer, size_t buffer_symbols,
                      uint32_t signal_range_min_ns, uint32_t signal_range_max_ns,
                      rmt_rx_callback_t callback, void *user_ctx = nullptr) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static esp_err_t ensureCallbackRuntime(void);
    static int findSlotByPin(int pin);
    static int findFreeSlot(void);
};

/// Global RMT convenience object.
extern Rmt rmt;

} // namespace esp32libfun

using esp32libfun::rmt;
using esp32libfun::Rmt;
using esp32libfun::rmt_symbol_t;
using esp32libfun::rmt_rx_callback_t;
