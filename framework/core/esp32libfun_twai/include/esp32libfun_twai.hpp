#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

namespace esp32libfun {

static constexpr size_t TWAI_MAX_DATA_LEN = 64;

struct TwaiFrame {
    uint32_t id = 0;
    uint8_t data[TWAI_MAX_DATA_LEN] = {};
    size_t len = 0;
    bool ext = false;
    bool rtr = false;
    bool fd = false;
    bool brs = false;
    uint64_t timestamp = 0;
};

struct TwaiStatus {
    int state = 0;
    uint16_t tx_error_count = 0;
    uint16_t rx_error_count = 0;
    uint32_t tx_queue_remaining = 0;
    uint32_t bus_error_count = 0;
};

class Twai {
public:
    static constexpr size_t MAX_NODES = 2;
    static constexpr uint32_t DEFAULT_BITRATE = 500000;
    static constexpr size_t DEFAULT_TX_QUEUE_DEPTH = 5;

    /// Creates and enables one on-chip TWAI node.
    ///
    /// @param tx_pin GPIO used as TWAI TX.
    /// @param rx_pin GPIO used as TWAI RX.
    /// @param bitrate Arbitration bitrate in bits per second.
    /// @param tx_queue_depth Driver TX queue depth in frames.
    /// @param loopback When true, received frames include this node's own transmissions.
    /// @param self_test When true, transmissions do not require ACK; useful for bench tests without another node.
    /// @param listen_only When true, the node monitors the bus and does not transmit or ACK.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t begin(int tx_pin,
                    int rx_pin,
                    uint32_t bitrate = DEFAULT_BITRATE,
                    size_t tx_queue_depth = DEFAULT_TX_QUEUE_DEPTH,
                    bool loopback = false,
                    bool self_test = false,
                    bool listen_only = false) const;
    /// Disables and releases one TWAI node.
    ///
    /// @param tx_pin TX pin that identifies the node.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t end(int tx_pin) const;
    /// Returns true when the selected TX pin already owns a TWAI node.
    ///
    /// @param tx_pin TX pin to check.
    /// @return true when the node exists.
    [[nodiscard]] bool ready(int tx_pin) const;
    /// Returns the raw `twai_node_handle_t` for advanced users.
    ///
    /// @param tx_pin TX pin that identifies the node.
    /// @return The native driver handle cast to `void *`, or `nullptr` if none.
    [[nodiscard]] void *node(int tx_pin) const;

    /// Queues one TWAI frame for transmission.
    ///
    /// @param tx_pin TX pin that identifies the node.
    /// @param frame Frame to transmit.
    /// @param timeout_ms Maximum queue wait time in milliseconds; `-1` waits forever.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t write(int tx_pin, const TwaiFrame &frame, int timeout_ms = 1000) const;
    /// Queues one classic TWAI frame for transmission.
    ///
    /// @param tx_pin TX pin that identifies the node.
    /// @param id Standard or extended CAN identifier.
    /// @param data Payload bytes.
    /// @param len Payload length, up to 8 bytes for classic TWAI.
    /// @param ext When true, sends an extended 29-bit ID.
    /// @param timeout_ms Maximum queue wait time in milliseconds; `-1` waits forever.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t write(int tx_pin, uint32_t id, const uint8_t *data, size_t len, bool ext = false, int timeout_ms = 1000) const;
    /// Reads one received frame from the internal RX queue.
    ///
    /// @param tx_pin TX pin that identifies the node.
    /// @param frame Receives the next frame.
    /// @param timeout_ms Maximum wait time in milliseconds; `-1` waits forever.
    /// @return `ESP_OK` on success, `ESP_ERR_TIMEOUT` on timeout, or another `esp_err_t`.
    esp_err_t read(int tx_pin, TwaiFrame *frame, int timeout_ms = 0) const;
    /// Blocks until all queued transmissions are done.
    ///
    /// @param tx_pin TX pin that identifies the node.
    /// @param timeout_ms Maximum wait time in milliseconds; `-1` waits forever.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t wait(int tx_pin, int timeout_ms = -1) const;
    /// Starts bus-off recovery for one node.
    ///
    /// @param tx_pin TX pin that identifies the node.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t recover(int tx_pin) const;
    /// Reads driver status and bus error counters.
    ///
    /// @param tx_pin TX pin that identifies the node.
    /// @param status Receives the current status.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t status(int tx_pin, TwaiStatus *status) const;
    /// Configures one mask acceptance filter.
    ///
    /// @param tx_pin TX pin that identifies the node.
    /// @param id Base identifier to match.
    /// @param mask Mask bits; `1` means the bit must match.
    /// @param ext When true, configures an extended-ID filter.
    /// @param filter_id Hardware filter index to configure.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t filter(int tx_pin, uint32_t id, uint32_t mask, bool ext = false, uint8_t filter_id = 0) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static esp_err_t ensureRxRuntime(void);
    static int findSlotByTxPin(int tx_pin);
    static int findFreeSlot(void);
};

/// Global TWAI convenience object.
extern Twai twai;

} // namespace esp32libfun

using esp32libfun::twai;
using esp32libfun::Twai;
using esp32libfun::TwaiFrame;
using esp32libfun::TwaiStatus;
