#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "sdkconfig.h"

#ifndef CONFIG_ESP32LIBFUN_AT_MAX_CMDS
#define CONFIG_ESP32LIBFUN_AT_MAX_CMDS 16
#endif

#define ESP32LIBFUN_AT_VERSION "v0.1.0"
#define ESP32LIBFUN_AT_VERSION_MAJOR 0
#define ESP32LIBFUN_AT_VERSION_MINOR 1
#define ESP32LIBFUN_AT_VERSION_PATCH 0

namespace esp32libfun {

/// Handler invoked when a matching AT command is dispatched.
///
/// @param args Raw argument text after the command, or an empty string when none.
using at_handler_t = void (*)(const char *args);

/// Describes one AT command entry and its optional help text.
struct at_command_t {
    const char *command;
    at_handler_t handler;
    const char *help;
};

/// Fixed-size AT command registry plus serial-backed console task.
class At {
public:
    static constexpr size_t MAX_COMMANDS = CONFIG_ESP32LIBFUN_AT_MAX_CMDS;
    static constexpr size_t MAX_COMMAND_LEN = 24;
    static constexpr size_t MAX_HELP_LEN = 96;

    /// Initializes the AT registry and synchronization primitives.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t init(void);
    /// Stops the console task and releases AT resources.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t deinit(void);
    /// Returns true when the AT subsystem is initialized.
    ///
    /// @return true when `init()` has already succeeded.
    [[nodiscard]] bool isInitialized(void) const;

    /// Starts the serial-backed AT console task.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t start(void);
    /// Stops the serial-backed AT console task.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t stop(void);

    /// Parses and dispatches a complete AT line without CR/LF.
    ///
    /// @param line Null-terminated command line to dispatch.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t feedLine(const char *line);
    /// Registers a command in the fixed-size internal registry.
    ///
    /// @param command Command string, e.g. `"AT+GPIO"`.
    /// @param handler Function invoked when the command matches.
    /// @param help Optional help text shown by `AT+HELP?`.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure
    ///         (e.g. `ESP_ERR_NO_MEM` when the registry is full).
    esp_err_t registerCmd(const char *command, at_handler_t handler, const char *help = nullptr);
    /// Removes a previously registered command.
    ///
    /// @param command Command string to remove.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t unregisterCmd(const char *command);

    /// Returns the number of registered commands currently available.
    ///
    /// @return Current number of registered commands.
    [[nodiscard]] size_t commandCount(void) const;
    /// Prints the built-in and registered command list.
    void help(void) const;
    /// Prints the framework version through the AT console.
    void version(void) const;
    /// Performs a software reset of the chip.
    void reset(void) const;
    /// Writes one formatted response line through the AT console.
    ///
    /// @param fmt `printf`-style format string.
    void writeLine(const char *fmt, ...) const;
    /// Writes one formatted error response line through the AT console.
    ///
    /// @param fmt `printf`-style format string.
    void writeError(const char *fmt, ...) const;

private:
    bool initialized_ = false;
    bool started_ = false;
};

/// Global AT convenience object.
extern At at;

/// Self-registering AT command plugin.
///
/// Declare a static instance in any component's .cpp file to register a
/// command without modifying the AT core. Captureless lambdas are the only
/// supported handler form and decay to at_handler_t with no heap.
///
/// @code
/// // esp_wifi.cpp
/// static AtRegistrar wifi_en { "WIFI+EN", [](const char*) { wifi.enable(); } };
/// static AtRegistrar wifi_dis{ "WIFI+DIS", [](const char*) { wifi.disable(); } };
/// @endcode
struct AtRegistrar {
    /// Registers `command` with `handler` at static-init time.
    ///
    /// @param command Command string, e.g. `"AT+WIFI+EN"`.
    /// @param handler Captureless-lambda-compatible handler function.
    /// @param help Optional help text shown by `AT+HELP?`.
    AtRegistrar(const char *command, at_handler_t handler, const char *help = nullptr) {
        at.registerCmd(command, handler, help);
    }
};

} // namespace esp32libfun

using esp32libfun::at;
using esp32libfun::at_command_t;
using esp32libfun::at_handler_t;
using esp32libfun::AtRegistrar;
