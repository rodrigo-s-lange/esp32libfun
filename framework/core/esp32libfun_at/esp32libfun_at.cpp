#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp32libfun_at.hpp"
#include "esp32libfun_serial.hpp"

namespace esp32libfun {

namespace {

static const char *TAG = "ESP32LIBFUN_AT";

struct StoredAtCommand {
    bool used = false;
    char command[At::MAX_COMMAND_LEN + 1] = {};
    at_handler_t handler = nullptr;
    bool has_help = false;
    char help[At::MAX_HELP_LEN + 1] = {};
};

StoredAtCommand s_commands[At::MAX_COMMANDS] = {};
size_t s_command_count = 0;
TaskHandle_t s_at_task_handle = nullptr;
SemaphoreHandle_t s_at_mutex = nullptr;

bool at_lock(TickType_t timeout = portMAX_DELAY)
{
    return (s_at_mutex != nullptr) && (xSemaphoreTake(s_at_mutex, timeout) == pdTRUE);
}

void at_unlock(void)
{
    if (s_at_mutex != nullptr) {
        xSemaphoreGive(s_at_mutex);
    }
}

const char *skipSeparators(const char *text)
{
    while (text != nullptr && (*text == '=' || *text == ' ' || *text == '\t')) {
        ++text;
    }
    return text;
}

bool copyString(char *dst, size_t dst_len, const char *src)
{
    if (dst == nullptr || dst_len == 0 || src == nullptr) {
        return false;
    }

    const size_t len = strlen(src);
    if (len >= dst_len) {
        return false;
    }

    memcpy(dst, src, len + 1);
    return true;
}

void atTask(void *arg)
{
    At *self = static_cast<At *>(arg);
    char buf[128] = {};
    size_t len = 0;

    while (true) {
        char ch = 0;
        if (serial.readByte(&ch) <= 0) {
            continue;
        }

        if (ch == '\b' || ch == 0x7f) {
            if (len > 0) {
                --len;
                serial.print("\b \b");
            }
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            serial.print("\r\n");
            if (len > 0) {
                buf[len] = '\0';
                self->feedLine(buf);
                len = 0;
            }
            continue;
        }

        if (ch >= 0x20 && ch < 0x7f && len + 1 < sizeof(buf)) {
            buf[len++] = ch;
            serial.print("%c", ch);
        }
    }
}

} // namespace

esp_err_t At::init(void)
{
    if (initialized_) {
        ESP_LOGW(TAG, "already initialized");
        return ESP_OK;
    }

    if (s_at_mutex == nullptr) {
        s_at_mutex = xSemaphoreCreateMutex();
        if (s_at_mutex == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    initialized_ = true;
    ESP_LOGI(TAG, "initialized");
    return ESP_OK;
}

esp_err_t At::deinit(void)
{
    if (!initialized_) {
        return ESP_OK;
    }

    stop();
    if (s_at_mutex != nullptr) {
        vSemaphoreDelete(s_at_mutex);
        s_at_mutex = nullptr;
    }
    initialized_ = false;
    ESP_LOGI(TAG, "deinitialized");
    return ESP_OK;
}

bool At::isInitialized(void) const
{
    return initialized_;
}

esp_err_t At::start(void)
{
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!serial.isInitialized()) {
        ESP_LOGE(TAG, "serial backend is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (started_) {
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreate(atTask, "libfun_at", 4096, this, 5, &s_at_task_handle);
    if (ok != pdPASS) {
        return ESP_FAIL;
    }

    started_ = true;
    ESP_LOGI(TAG, "console task started");
    return ESP_OK;
}

esp_err_t At::stop(void)
{
    if (!started_) {
        return ESP_OK;
    }

    if (s_at_task_handle != nullptr) {
        vTaskDelete(s_at_task_handle);
        s_at_task_handle = nullptr;
    }

    started_ = false;
    ESP_LOGI(TAG, "console task stopped");
    return ESP_OK;
}

esp_err_t At::feedLine(const char *line)
{
    if (line == nullptr || line[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (strcmp(line, "AT") == 0) {
        serial.println(G "OK");
        return ESP_OK;
    }

    if (strcmp(line, "AT+HELP?") == 0) {
        help();
        return ESP_OK;
    }

    if (strcmp(line, "AT+VER?") == 0) {
        version();
        return ESP_OK;
    }

    const char *separator = nullptr;
    for (const char *cursor = line; *cursor != '\0'; ++cursor) {
        if (*cursor == '=' || *cursor == ' ' || *cursor == '\t') {
            separator = cursor;
            break;
        }
    }

    const size_t command_length = (separator != nullptr) ? static_cast<size_t>(separator - line) : strlen(line);
    const char *args = (separator != nullptr) ? skipSeparators(separator) : "";

    if (at_lock()) {
        for (size_t i = 0; i < s_command_count; ++i) {
            if (!s_commands[i].used) {
                continue;
            }

            const size_t stored_length = strlen(s_commands[i].command);
            if (stored_length == command_length &&
                strncmp(s_commands[i].command, line, command_length) == 0) {
                at_handler_t handler = s_commands[i].handler;
                at_unlock();
                handler(args);
                return ESP_OK;
            }
        }
        at_unlock();
    }

    writeError("unknown command");
    return ESP_ERR_NOT_FOUND;
}

esp_err_t At::add(const char *command, at_handler_t handler, const char *help)
{
    if (command == nullptr || handler == nullptr || command[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(command) > MAX_COMMAND_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    if (help != nullptr && strlen(help) > MAX_HELP_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    // s_at_mutex is nullptr during static-constructor phase (before init()).
    // Static init runs single-threaded, so skipping the lock is safe there.
    const bool use_lock = (s_at_mutex != nullptr);
    if (use_lock && !at_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < s_command_count; ++i) {
        if (s_commands[i].used && strcmp(s_commands[i].command, command) == 0) {
            if (use_lock) at_unlock();
            return ESP_ERR_INVALID_STATE;
        }
    }

    if (s_command_count >= MAX_COMMANDS) {
        if (use_lock) at_unlock();
        return ESP_ERR_NO_MEM;
    }

    StoredAtCommand &slot = s_commands[s_command_count];
    slot.used = true;
    slot.handler = handler;
    slot.has_help = (help != nullptr) && (help[0] != '\0');

    if (!copyString(slot.command, sizeof(slot.command), command) ||
        (slot.has_help && !copyString(slot.help, sizeof(slot.help), help))) {
        slot = {};
        if (use_lock) at_unlock();
        return ESP_ERR_INVALID_ARG;
    }

    ++s_command_count;
    if (use_lock) at_unlock();
    return ESP_OK;
}

esp_err_t At::registerCmd(const char *command, at_handler_t handler, const char *help)
{
    return add(command, handler, help);
}

esp_err_t At::unregisterCmd(const char *command)
{
    if (command == nullptr || command[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (!at_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < s_command_count; ++i) {
        if (s_commands[i].used && strcmp(s_commands[i].command, command) == 0) {
            for (size_t j = i; j + 1 < s_command_count; ++j) {
                s_commands[j] = s_commands[j + 1];
            }
            s_commands[s_command_count - 1] = {};
            --s_command_count;
            at_unlock();
            return ESP_OK;
        }
    }

    at_unlock();
    return ESP_ERR_NOT_FOUND;
}

size_t At::commandCount(void) const
{
    if (!at_lock()) {
        return 0;
    }

    const size_t count = s_command_count;
    at_unlock();
    return count;
}

void At::help(void) const
{
    StoredAtCommand commands[At::MAX_COMMANDS] = {};
    size_t count = 0;

    if (at_lock()) {
        count = s_command_count;
        for (size_t i = 0; i < count; ++i) {
            commands[i] = s_commands[i];
        }
        at_unlock();
    }

    serial.println(O "AT commands:");
    serial.println(C "  AT+HELP?");
    serial.println(C "  AT+VER?");

    for (size_t i = 0; i < count; ++i) {
        if (!commands[i].used) {
            continue;
        }

        if (commands[i].has_help) {
            serial.println(C "  %-16s" M " %s", commands[i].command, commands[i].help);
        } else {
            serial.println(C "  %s", commands[i].command);
        }
    }
}

void At::version(void) const
{
    serial.println(O "esp32libfun " G "AT ready");
}

void At::writeLine(const char *fmt, ...) const
{
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    serial.println("%s", buffer);
}

void At::writeError(const char *fmt, ...) const
{
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    serial.println(R "ERROR: %s" W, buffer);
}

At at;

} // namespace esp32libfun
