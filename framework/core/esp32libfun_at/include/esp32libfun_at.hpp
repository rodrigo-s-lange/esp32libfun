#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "sdkconfig.h"

namespace esp32libfun {

using at_handler_t = void (*)(const char *args);

struct at_command_t {
    const char *command;
    at_handler_t handler;
    const char *help;
};

class At {
public:
    static constexpr size_t MAX_COMMANDS = CONFIG_ESP32LIBFUN_AT_MAX_CMDS;
    static constexpr size_t MAX_COMMAND_LEN = 24;
    static constexpr size_t MAX_HELP_LEN = 96;

    /// Initializes the AT registry and synchronization primitives.
    esp_err_t init(void);
    /// Stops the console task and releases AT resources.
    esp_err_t deinit(void);
    [[nodiscard]] bool isInitialized(void) const;

    /// Starts the serial-backed AT console task.
    esp_err_t start(void);
    /// Stops the serial-backed AT console task.
    esp_err_t stop(void);

    /// Parses and dispatches a complete AT line without CR/LF.
    esp_err_t feedLine(const char *line);
    /// Adds one command to the fixed-size internal registry.
    esp_err_t add(const char *command, at_handler_t handler, const char *help = nullptr);
    /// Registers a command in the fixed-size internal registry.
    esp_err_t registerCmd(const char *command, at_handler_t handler, const char *help = nullptr);
    /// Removes a previously registered command.
    esp_err_t unregisterCmd(const char *command);

    [[nodiscard]] size_t commandCount(void) const;
    void help(void) const;
    void version(void) const;
    void writeLine(const char *fmt, ...) const;
    void writeError(const char *fmt, ...) const;

private:
    bool initialized_ = false;
    bool started_ = false;
};

extern At at;

/// Self-registering AT command plugin.
///
/// Declare a static instance in any component's .cpp file to register a
/// command without modifying At or Runtime.  Captureless lambdas are the
/// only supported handler form — they decay to at_handler_t with no heap.
///
/// @code
/// // esp_wifi.cpp
/// static AtRegistrar wifi_en { "WIFI+EN", [](const char*) { wifi.enable(); } };
/// static AtRegistrar wifi_dis{ "WIFI+DIS", [](const char*) { wifi.disable(); } };
/// @endcode
struct AtRegistrar {
    AtRegistrar(const char *command, at_handler_t handler, const char *help = nullptr) {
        at.add(command, handler, help);
    }
};

} // namespace esp32libfun

using esp32libfun::at;
using esp32libfun::at_command_t;
using esp32libfun::at_handler_t;
using esp32libfun::AtRegistrar;
