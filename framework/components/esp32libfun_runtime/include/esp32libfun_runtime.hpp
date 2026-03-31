#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "sdkconfig.h"

namespace esp32libfun {

/// Descriptor and self-registering handle for a runtime module.
///
/// Declare a static instance in any component's .cpp to register init/deinit
/// hooks without touching Runtime or the runtime entry points.
/// Captureless lambdas are the only supported form and decay to void(*)().
///
/// @code
/// static RuntimeRegistrar wifi_rt {
///     "wifi",
///     []() { wifi.init(); },
///     []() { wifi.deinit(); },
/// };
/// @endcode
struct RuntimeRegistrar {
    const char *name;
    void (*init_fn)();
    void (*deinit_fn)();

    RuntimeRegistrar(const char *name, void (*init)(), void (*deinit)() = nullptr);
};

/// Lifecycle orchestrator for self-registered modules.
///
/// Holds a fixed-size table of RuntimeRegistrar pointers populated during
/// static initialization.
class Runtime {
public:
    static constexpr size_t MAX_MODULES = CONFIG_ESP32LIBFUN_RUNTIME_MAX_MODULES;

    /// Adds a module to the table.
    esp_err_t add(const RuntimeRegistrar *mod);

    /// Calls init_fn() on every registered module in registration order.
    void initAll(void);

    /// Calls deinit_fn() on every registered module in reverse order.
    void deinitAll(void);

    [[nodiscard]] size_t moduleCount(void) const;
    [[nodiscard]] bool isInitialized(void) const;

private:
    const RuntimeRegistrar *modules_[MAX_MODULES] = {};
    size_t count_ = 0;
    bool initialized_ = false;
};

extern Runtime runtime;

} // namespace esp32libfun

using esp32libfun::Runtime;
using esp32libfun::RuntimeRegistrar;
using esp32libfun::runtime;

/// Runs initAll() on every self-registered RuntimeRegistrar.
/// Called by esp32libfun_init() when runtime support is enabled.
void esp32libfun_runtime_init(void);

/// Calls deinitAll() on every registered RuntimeRegistrar in reverse order.
void esp32libfun_runtime_deinit(void);
