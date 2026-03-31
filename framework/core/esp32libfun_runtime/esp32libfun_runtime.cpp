#include <string.h>

#include "esp_log.h"
#include "esp32libfun_runtime.hpp"

namespace esp32libfun {

namespace {
static const char *TAG = "ESP32LIBFUN_RT";
} // namespace

// ---------------------------------------------------------------------------
// RuntimeRegistrar
// ---------------------------------------------------------------------------

RuntimeRegistrar::RuntimeRegistrar(const char *n, void (*init)(), void (*deinit)())
    : name(n), init_fn(init), deinit_fn(deinit)
{
    runtime.add(this);
}

// ---------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------

esp_err_t Runtime::add(const RuntimeRegistrar *mod)
{
    if (mod == nullptr || mod->name == nullptr || mod->init_fn == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < count_; ++i) {
        if (strcmp(modules_[i]->name, mod->name) == 0) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    if (count_ >= MAX_MODULES) {
        return ESP_ERR_NO_MEM;
    }

    modules_[count_++] = mod;
    return ESP_OK;
}

void Runtime::initAll(void)
{
    if (initialized_) {
        return;
    }

    for (size_t i = 0; i < count_; ++i) {
        ESP_LOGI(TAG, "init %s", modules_[i]->name);
        modules_[i]->init_fn();
    }

    initialized_ = true;
}

void Runtime::deinitAll(void)
{
    if (!initialized_) {
        return;
    }

    for (size_t i = count_; i > 0; --i) {
        if (modules_[i - 1]->deinit_fn != nullptr) {
            ESP_LOGI(TAG, "deinit %s", modules_[i - 1]->name);
            modules_[i - 1]->deinit_fn();
        }
    }

    initialized_ = false;
}

size_t Runtime::moduleCount(void) const
{
    return count_;
}

bool Runtime::isInitialized(void) const
{
    return initialized_;
}

Runtime runtime;

} // namespace esp32libfun

// ---------------------------------------------------------------------------
// C linkage entry point
// ---------------------------------------------------------------------------

void esp32libfun_runtime_init(void)
{
    esp32libfun::runtime.initAll();
}

void esp32libfun_runtime_deinit(void)
{
    esp32libfun::runtime.deinitAll();
}
