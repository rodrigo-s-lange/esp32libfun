#include "esp32libfun_i2c.hpp"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"

namespace esp32libfun {

namespace {

static const char *TAG = "ESP32LIBFUN_I2C";
constexpr uint8_t kGlitchIgnoreCount = 7;

struct BusState {
    bool active = false;
    int sda_pin = -1;
    int scl_pin = -1;
    uint32_t speed_hz = 0;
    bool internal_pullup = false;
    uint32_t ref_count = 0;
    i2c_master_bus_handle_t handle = nullptr;
};

struct DeviceState {
    bool used = false;
    int port = -1;
    uint16_t address = 0;
    int addr_bits = I2C_ADDR_7BIT;
    uint32_t speed_hz = 0;
    uint32_t ref_count = 0;
    i2c_master_dev_handle_t handle = nullptr;
};

class LockGuard {
public:
    explicit LockGuard(SemaphoreHandle_t mutex)
        : mutex_(mutex), locked_((mutex != nullptr) && (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE))
    {
    }

    ~LockGuard(void)
    {
        if (locked_) {
            xSemaphoreGive(mutex_);
        }
    }

    [[nodiscard]] bool locked(void) const
    {
        return locked_;
    }

private:
    SemaphoreHandle_t mutex_ = nullptr;
    bool locked_ = false;
};

BusState s_buses[I2c::MAX_BUSES] = {};
DeviceState s_devices[I2c::MAX_DEVICES] = {};
SemaphoreHandle_t s_i2c_mutex = nullptr;

i2c_addr_bit_len_t toAddrLength(int addr_bits)
{
    return (addr_bits == I2C_ADDR_10BIT) ? I2C_ADDR_BIT_LEN_10 : I2C_ADDR_BIT_LEN_7;
}

} // namespace

esp_err_t I2c::ensureSyncPrimitives(void)
{
    if (s_i2c_mutex == nullptr) {
        s_i2c_mutex = xSemaphoreCreateMutex();
        if (s_i2c_mutex == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

bool I2c::isValidPort(int port)
{
    return (port >= 0) && (port < static_cast<int>(MAX_BUSES));
}

bool I2c::isValidAddress(uint16_t address, int addr_bits)
{
    if (addr_bits == I2C_ADDR_7BIT) {
        return address <= 0x7F;
    }

    if (addr_bits == I2C_ADDR_10BIT) {
        return address <= 0x3FF;
    }

    return false;
}

int I2c::findDeviceIndex(uint16_t address, int port)
{
    for (size_t i = 0; i < MAX_DEVICES; ++i) {
        if (s_devices[i].used &&
            s_devices[i].port == port &&
            s_devices[i].address == address) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

esp_err_t I2c::begin(int sda_pin, int scl_pin, uint32_t speed_hz, int port, bool internal_pullup) const
{
    if (!isValidPort(port) ||
        !GPIO_IS_VALID_GPIO(static_cast<gpio_num_t>(sda_pin)) ||
        !GPIO_IS_VALID_GPIO(static_cast<gpio_num_t>(scl_pin)) ||
        speed_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_i2c_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    BusState &bus = s_buses[port];
    if (bus.active) {
        const bool same_config =
            (bus.sda_pin == sda_pin) &&
            (bus.scl_pin == scl_pin) &&
            (bus.speed_hz == speed_hz) &&
            (bus.internal_pullup == internal_pullup);
        if (!same_config) {
            return ESP_ERR_INVALID_STATE;
        }

        ++bus.ref_count;
        ESP_LOGD(TAG, "bus %d ref_count=%lu", port, static_cast<unsigned long>(bus.ref_count));
        return ESP_OK;
    }

    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port = static_cast<i2c_port_num_t>(port);
    cfg.sda_io_num = static_cast<gpio_num_t>(sda_pin);
    cfg.scl_io_num = static_cast<gpio_num_t>(scl_pin);
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = kGlitchIgnoreCount;
    cfg.trans_queue_depth = 0;
    cfg.flags.enable_internal_pullup = internal_pullup;

    err = i2c_new_master_bus(&cfg, &bus.handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus(port=%d) failed: %s", port, esp_err_to_name(err));
        return err;
    }

    bus.active = true;
    bus.sda_pin = sda_pin;
    bus.scl_pin = scl_pin;
    bus.speed_hz = speed_hz;
    bus.internal_pullup = internal_pullup;
    bus.ref_count = 1;
    ESP_LOGI(TAG, "bus %d initialized on SDA=%d SCL=%d @ %lu Hz", port, sda_pin, scl_pin, static_cast<unsigned long>(speed_hz));
    return ESP_OK;
}

esp_err_t I2c::end(int port) const
{
    if (!isValidPort(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_i2c_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    BusState &bus = s_buses[port];
    if (!bus.active) {
        return ESP_OK;
    }

    if (bus.ref_count > 1) {
        --bus.ref_count;
        ESP_LOGD(TAG, "bus %d ref_count=%lu", port, static_cast<unsigned long>(bus.ref_count));
        return ESP_OK;
    }

    for (size_t i = 0; i < MAX_DEVICES; ++i) {
        if (!s_devices[i].used || s_devices[i].port != port) {
            continue;
        }

        ESP_LOGE(TAG, "bus %d still has device 0x%02X registered", port, s_devices[i].address);
        return ESP_ERR_INVALID_STATE;
    }

    err = i2c_del_master_bus(bus.handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_del_master_bus(port=%d) failed: %s", port, esp_err_to_name(err));
        return err;
    }

    bus = {};
    ESP_LOGI(TAG, "bus %d deinitialized", port);
    return ESP_OK;
}

bool I2c::ready(int port) const
{
    if (!isValidPort(port) || s_i2c_mutex == nullptr) {
        return false;
    }

    LockGuard guard(s_i2c_mutex);
    if (!guard.locked()) {
        return false;
    }

    return s_buses[port].active;
}

bool I2c::has(uint16_t address, int port) const
{
    if (!isValidPort(port) || s_i2c_mutex == nullptr) {
        return false;
    }

    LockGuard guard(s_i2c_mutex);
    if (!guard.locked()) {
        return false;
    }

    return findDeviceIndex(address, port) >= 0;
}

esp_err_t I2c::add(uint16_t address, int port, uint32_t speed_hz, int addr_bits) const
{
    if (!isValidPort(port) || !isValidAddress(address, addr_bits)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_i2c_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    BusState &bus = s_buses[port];
    if (!bus.active) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t final_speed_hz = (speed_hz == 0) ? bus.speed_hz : speed_hz;
    if (final_speed_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int existing_index = findDeviceIndex(address, port);
    if (existing_index >= 0) {
        DeviceState &device = s_devices[existing_index];
        const bool same_config =
            (device.addr_bits == addr_bits) &&
            (device.speed_hz == final_speed_hz);
        if (!same_config) {
            return ESP_ERR_INVALID_STATE;
        }

        ++device.ref_count;
        ESP_LOGD(TAG, "device 0x%02X on bus %d ref_count=%lu", address, port, static_cast<unsigned long>(device.ref_count));
        return ESP_OK;
    }

    size_t free_index = MAX_DEVICES;
    for (size_t i = 0; i < MAX_DEVICES; ++i) {
        if (!s_devices[i].used) {
            free_index = i;
            break;
        }
    }
    if (free_index >= MAX_DEVICES) {
        return ESP_ERR_NO_MEM;
    }

    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = toAddrLength(addr_bits);
    cfg.device_address = address;
    cfg.scl_speed_hz = final_speed_hz;
    cfg.scl_wait_us = 0;
    cfg.flags.disable_ack_check = false;

    i2c_master_dev_handle_t handle = nullptr;
    err = i2c_master_bus_add_device(bus.handle, &cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to add device 0x%02X on bus %d: %s", address, port, esp_err_to_name(err));
        return err;
    }

    DeviceState &device = s_devices[free_index];
    device.used = true;
    device.port = port;
    device.address = address;
    device.addr_bits = addr_bits;
    device.speed_hz = final_speed_hz;
    device.ref_count = 1;
    device.handle = handle;
    return ESP_OK;
}

esp_err_t I2c::remove(uint16_t address, int port) const
{
    if (!isValidPort(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_i2c_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int index = findDeviceIndex(address, port);
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    if (s_devices[index].ref_count > 1) {
        --s_devices[index].ref_count;
        ESP_LOGD(TAG, "device 0x%02X on bus %d ref_count=%lu", address, port, static_cast<unsigned long>(s_devices[index].ref_count));
        return ESP_OK;
    }

    err = i2c_master_bus_rm_device(s_devices[index].handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to remove device 0x%02X from bus %d: %s", address, port, esp_err_to_name(err));
        return err;
    }

    s_devices[index] = {};
    return ESP_OK;
}

esp_err_t I2c::probe(uint16_t address, int port, int timeout_ms) const
{
    if (!isValidPort(port) || address > 0x7F) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_i2c_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const BusState &bus = s_buses[port];
    if (!bus.active) {
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_probe(bus.handle, address, timeout_ms);
}

esp_err_t I2c::write(uint16_t address, const uint8_t *data, size_t len, int port, int timeout_ms) const
{
    if (!isValidPort(port) || data == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_i2c_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int index = findDeviceIndex(address, port);
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    return i2c_master_transmit(s_devices[index].handle, data, len, timeout_ms);
}

esp_err_t I2c::read(uint16_t address, uint8_t *data, size_t len, int port, int timeout_ms) const
{
    if (!isValidPort(port) || data == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_i2c_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int index = findDeviceIndex(address, port);
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    return i2c_master_receive(s_devices[index].handle, data, len, timeout_ms);
}

esp_err_t I2c::writeRead(uint16_t address, const uint8_t *write_data, size_t write_len, uint8_t *read_data, size_t read_len, int port, int timeout_ms) const
{
    if (!isValidPort(port) ||
        write_data == nullptr || write_len == 0 ||
        read_data == nullptr || read_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_i2c_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int index = findDeviceIndex(address, port);
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    return i2c_master_transmit_receive(
        s_devices[index].handle,
        write_data,
        write_len,
        read_data,
        read_len,
        timeout_ms);
}

esp_err_t I2c::regWrite(uint16_t address, uint8_t reg, const uint8_t *data, size_t len, int port, int timeout_ms) const
{
    if (!isValidPort(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len > 0 && data == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_i2c_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int index = findDeviceIndex(address, port);
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    if (len == 0) {
        return i2c_master_transmit(s_devices[index].handle, &reg, 1, timeout_ms);
    }

    i2c_master_transmit_multi_buffer_info_t buffers[2] = {
        {
            .write_buffer = &reg,
            .buffer_size = 1,
        },
        {
            .write_buffer = data,
            .buffer_size = len,
        },
    };

    return i2c_master_multi_buffer_transmit(s_devices[index].handle, buffers, 2, timeout_ms);
}

esp_err_t I2c::regWrite8(uint16_t address, uint8_t reg, uint8_t value, int port, int timeout_ms) const
{
    return regWrite(address, reg, &value, 1, port, timeout_ms);
}

esp_err_t I2c::regRead(uint16_t address, uint8_t reg, uint8_t *data, size_t len, int port, int timeout_ms) const
{
    if (data == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    return writeRead(address, &reg, 1, data, len, port, timeout_ms);
}

esp_err_t I2c::regRead8(uint16_t address, uint8_t reg, uint8_t *value, int port, int timeout_ms) const
{
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    return regRead(address, reg, value, 1, port, timeout_ms);
}

esp_err_t I2c::reset(int port) const
{
    if (!isValidPort(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_i2c_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const BusState &bus = s_buses[port];
    if (!bus.active) {
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_bus_reset(bus.handle);
}

I2c i2c;

} // namespace esp32libfun
