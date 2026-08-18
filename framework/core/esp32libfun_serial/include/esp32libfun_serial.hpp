#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/*
 * ANSI color macros
 *
 * Convention:
 *   G  - OK / success / confirmed state
 *   Y  - warning / alert
 *   R  - error / failure
 *   O  - library tag / module identifier
 *   C  - general info / values
 *   M  - secondary info / highlights
 *   B  - debug / low-priority info
 *   P  - decorative / structural
 *   K  - user input color (applied automatically after print/println)
 *   W  - full reset (optional - print/println reset automatically)
 */
#define G "\033[32m"
#define Y "\033[33m"
#define R "\033[31m"
#define O "\033[38;5;208m"
#define C "\033[36m"
#define M "\033[95m"
#define B "\033[34m"
#define P "\033[35m"
#define K "\033[0m"
#define W "\033[0m"

#define ESP32LIBFUN_SERIAL_VERSION "v0.2.0"
#define ESP32LIBFUN_SERIAL_VERSION_MAJOR 0
#define ESP32LIBFUN_SERIAL_VERSION_MINOR 2
#define ESP32LIBFUN_SERIAL_VERSION_PATCH 0

namespace esp32libfun {

/// Thin wrapper around the active ESP-IDF console backend.
class Serial {
public:
    /// Initializes the configured ESP-IDF console backend.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t init(void);
    /// Deinitializes the configured console backend.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t deinit(void);
    /// Returns true when the serial backend is initialized and ready.
    ///
    /// @return true when `init()` has already succeeded.
    bool isInitialized(void) const;
    /// Enables or disables local echo for readLine().
    ///
    /// @param enabled true to echo typed characters back, false to stay silent.
    void setEcho(bool enabled);
    /// Returns true when readLine() echoes typed characters.
    ///
    /// @return true when local echo is enabled.
    bool echoEnabled(void) const;
    /// Enables or disables the optional serial AT command set.
    ///
    /// When enabled, this registers the serial sidecar commands such as
    /// `AT+SERIALECHO?` and `AT+SERIALBACKEND?`.
    ///
    /// @param enable true to register the sidecar commands, false to unregister them.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t at(bool enable = true);
    /// Returns true when the serial AT command set is registered.
    ///
    /// @return true when the sidecar commands are currently registered.
    bool atEnabled(void) const;

    /// Returns one byte from the active console backend, or a negative value on timeout.
    ///
    /// Access to the RX path is serialized internally.
    ///
    /// @param ch Receives the read byte.
    /// @return `0` on success, or a negative value on timeout or failure.
    int readByte(char *ch) const;
    /// Reads one line from the active console backend.
    ///
    /// Accepts `\n`, `\r`, and `\r\n` line endings.
    /// Access to the RX path is serialized internally.
    ///
    /// @param buffer Buffer that receives the null-terminated line.
    /// @param length Capacity of `buffer`, in bytes.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t readLine(char *buffer, size_t length) const;

    /// Returns the active console backend name for logs and diagnostics.
    ///
    /// @return Backend name as a static string.
    const char *backend(void) const;

    /// Writes one formatted string without a trailing line break.
    ///
    /// Access to the TX path is serialized internally.
    ///
    /// @param fmt `printf`-style format string.
    void print(const char *fmt, ...) const __attribute__((format(printf, 2, 3)));
    /// Writes one formatted string followed by CRLF.
    ///
    /// Access to the TX path is serialized internally.
    ///
    /// @param fmt `printf`-style format string.
    void println(const char *fmt, ...) const __attribute__((format(printf, 2, 3)));

private:
    static esp_err_t ensureSyncPrimitives(void);

    bool initialized_ = false;
    bool echo_enabled_ = false;
};

/// Global serial convenience object bound to the active console backend.
extern Serial serial;

} // namespace esp32libfun

using esp32libfun::serial;
