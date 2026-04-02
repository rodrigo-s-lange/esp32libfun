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

class Serial {
public:
    /// Initializes the configured ESP-IDF console backend.
    esp_err_t init(void);
    /// Deinitializes the configured console backend.
    esp_err_t deinit(void);
    /// Returns true when the serial backend is initialized and ready.
    bool isInitialized(void) const;

    /// Returns one byte from the active console backend, or a negative value on timeout.
    ///
    /// Access to the RX path is serialized internally.
    int readByte(char *ch) const;
    /// Reads one line from the active console backend.
    ///
    /// Accepts `\n`, `\r`, and `\r\n` line endings.
    /// Access to the RX path is serialized internally.
    esp_err_t readLine(char *buffer, size_t length) const;

    /// Returns the active console backend name for logs and diagnostics.
    const char *backend(void) const;

    /// Writes one formatted string without a trailing line break.
    ///
    /// Access to the TX path is serialized internally.
    void print(const char *fmt, ...) const __attribute__((format(printf, 2, 3)));
    /// Writes one formatted string followed by CRLF.
    ///
    /// Access to the TX path is serialized internally.
    void println(const char *fmt, ...) const __attribute__((format(printf, 2, 3)));

private:
    static esp_err_t ensureSyncPrimitives(void);

    bool initialized_ = false;
};

extern Serial serial;
