#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/*
 * ANSI color macros
 *
 * Convention:
 *   G  — OK / success / confirmed state
 *   Y  — warning / alert
 *   R  — error / failure
 *   O  — library tag / module identifier  e.g. serial.println(O "[esp32libfun_gpio]" C " initialized")
 *   C  — general info / values
 *   M  — secondary info / highlights
 *   B  — debug / low-priority info
 *   P  — decorative / structural
 *   K  — user input color (applied automatically after print/println)
 *   W  — full reset (optional — print/println reset automatically)
 */
#define G "\033[32m"           /* green       — OK / success          */
#define Y "\033[33m"           /* yellow      — warning / alert        */
#define R "\033[31m"           /* red         — error / failure        */
#define O "\033[38;5;208m"     /* orange      — lib tag / module id    */
#define C "\033[36m"           /* cyan        — general info           */
#define M "\033[95m"           /* magenta     — secondary info         */
#define B "\033[34m"           /* blue        — debug / low priority   */
#define P "\033[35m"           /* purple      — decorative             */
#define K "\033[0m"            /* reset       — user input (auto)      */
#define W "\033[0m"            /* reset       — full reset             */

class Serial {
public:
    /// Initializes the configured ESP-IDF console backend.
    esp_err_t init(void);
    /// Deinitializes the configured console backend.
    esp_err_t deinit(void);
    /// Returns true when the serial backend is initialized and ready.
    bool isInitialized(void) const;

    /// Returns one byte from the active console backend, or a negative value on timeout.
    int       readByte(char *ch) const;
    /// Reads one line from the active console backend.
    ///
    /// Accepts `\\n`, `\\r`, and `\\r\\n` line endings.
    esp_err_t readLine(char *buffer, size_t length) const;

    /// Returns the active console backend name for logs and diagnostics.
    const char *backend(void) const;

    void print(const char *fmt, ...)   const __attribute__((format(printf, 2, 3)));
    void println(const char *fmt, ...) const __attribute__((format(printf, 2, 3)));

private:
    bool initialized_ = false;
};

extern Serial serial;
