#include "esp_pca9685.hpp"

#include <math.h>

#include "esp_log.h"
#include "esp32libfun_i2c.hpp"

namespace {

static const char *TAG = "ESP_PCA9685";

constexpr uint8_t kRegMode1 = 0x00;
constexpr uint8_t kRegMode2 = 0x01;
constexpr uint8_t kRegLed0OnL = 0x06;
constexpr uint8_t kRegPreScale = 0xFE;

constexpr uint8_t kMode1Restart = 0x80;
constexpr uint8_t kMode1Ai = 0x20;
constexpr uint8_t kMode1Sleep = 0x10;
constexpr uint8_t kMode1AllCall = 0x01;

constexpr uint8_t kMode2OutDrv = 0x04;
constexpr uint32_t kOscillatorHz = 25000000;
constexpr uint16_t kPwmResolution = 4096;
constexpr int kProbeTimeoutMs = 100;

} // namespace

namespace esp_pca9685 {

bool Pca9685::isValidChannel(uint8_t channel)
{
    return channel < CHANNELS;
}

bool Pca9685::isValidFrequency(uint16_t frequency_hz)
{
    return frequency_hz >= MIN_PWM_HZ && frequency_hz <= MAX_PWM_HZ;
}

uint8_t Pca9685::ledBaseRegister(uint8_t channel)
{
    return static_cast<uint8_t>(kRegLed0OnL + (channel * 4));
}

uint8_t Pca9685::computePrescale(uint16_t frequency_hz)
{
    const double prescale = (static_cast<double>(kOscillatorHz) / (kPwmResolution * static_cast<double>(frequency_hz))) - 1.0;
    const long rounded = lround(prescale);
    if (rounded < 3) {
        return 3;
    }
    if (rounded > 255) {
        return 255;
    }
    return static_cast<uint8_t>(rounded);
}

esp_err_t Pca9685::ensureReady(void) const
{
    if (!initialized_ || !i2c.ready(port_) || !i2c.has(address_, port_)) {
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

esp_err_t Pca9685::init(uint16_t address, int port, uint16_t frequency_hz)
{
    if (!isValidFrequency(frequency_hz) || address > 0x7F) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!i2c.ready(port)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (initialized_) {
        if (address_ != address || port_ != port) {
            return ESP_ERR_INVALID_STATE;
        }

        return freq(frequency_hz);
    }

    esp_err_t err = i2c.probe(address, port, kProbeTimeoutMs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "probe failed at 0x%02X on bus %d: %s", address, port, esp_err_to_name(err));
        return err;
    }

    err = i2c.add(address, port);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add failed at 0x%02X on bus %d: %s", address, port, esp_err_to_name(err));
        return err;
    }

    initialized_ = true;
    address_ = address;
    port_ = port;
    frequency_hz_ = frequency_hz;

    err = freq(frequency_hz);
    if (err != ESP_OK) {
        i2c.remove(address_, port_);
        initialized_ = false;
        address_ = DEFAULT_ADDRESS;
        port_ = 0;
        frequency_hz_ = DEFAULT_PWM_HZ;
        return err;
    }

    ESP_LOGI(TAG, "initialized at 0x%02X on I2C bus %d @ %u Hz", address_, port_, static_cast<unsigned>(frequency_hz_));
    return ESP_OK;
}

esp_err_t Pca9685::end(void)
{
    if (!initialized_) {
        return ESP_OK;
    }

    esp_err_t err = i2c.remove(address_, port_);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGE(TAG, "remove failed at 0x%02X on bus %d: %s", address_, port_, esp_err_to_name(err));
        return err;
    }

    initialized_ = false;
    address_ = DEFAULT_ADDRESS;
    port_ = 0;
    frequency_hz_ = DEFAULT_PWM_HZ;
    return ESP_OK;
}

bool Pca9685::ready(void) const
{
    return ensureReady() == ESP_OK;
}

esp_err_t Pca9685::freq(uint16_t frequency_hz)
{
    if (!isValidFrequency(frequency_hz)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureReady();
    if (err != ESP_OK) {
        return err;
    }

    uint8_t mode1 = 0;
    err = i2c.regRead8(address_, kRegMode1, &mode1, port_);
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t prescale = computePrescale(frequency_hz);
    const uint8_t sleep_mode = static_cast<uint8_t>((mode1 | kMode1Sleep) & ~kMode1Restart);
    const uint8_t running_mode = static_cast<uint8_t>((mode1 | kMode1Ai | kMode1AllCall) & ~kMode1Sleep);

    err = i2c.regWrite8(address_, kRegMode1, sleep_mode, port_);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c.regWrite8(address_, kRegPreScale, prescale, port_);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c.regWrite8(address_, kRegMode2, kMode2OutDrv, port_);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c.regWrite8(address_, kRegMode1, running_mode, port_);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c.regWrite8(address_, kRegMode1, static_cast<uint8_t>(running_mode | kMode1Restart), port_);
    if (err != ESP_OK) {
        return err;
    }

    frequency_hz_ = frequency_hz;
    return ESP_OK;
}

esp_err_t Pca9685::pwm(uint8_t channel, uint16_t on_count, uint16_t off_count, bool full_on, bool full_off) const
{
    if (!isValidChannel(channel) || on_count > 4095 || off_count > 4095) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureReady();
    if (err != ESP_OK) {
        return err;
    }

    uint8_t payload[4] = {
        static_cast<uint8_t>(on_count & 0xFF),
        static_cast<uint8_t>(((on_count >> 8) & 0x0F) | (full_on ? 0x10 : 0x00)),
        static_cast<uint8_t>(off_count & 0xFF),
        static_cast<uint8_t>(((off_count >> 8) & 0x0F) | (full_off ? 0x10 : 0x00)),
    };

    return i2c.regWrite(address_, ledBaseRegister(channel), payload, sizeof(payload), port_);
}

esp_err_t Pca9685::duty(uint8_t channel, uint8_t percent) const
{
    if (percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    if (percent == 0) {
        return off(channel);
    }

    if (percent == 100) {
        return on(channel);
    }

    return pwm(channel, 0, dutyCount(percent), false, false);
}

esp_err_t Pca9685::on(uint8_t channel) const
{
    return pwm(channel, 0, 0, true, false);
}

esp_err_t Pca9685::off(uint8_t channel) const
{
    return pwm(channel, 0, 0, false, true);
}

esp_err_t Pca9685::read(uint8_t channel, uint16_t *on_count, uint16_t *off_count, bool *full_on, bool *full_off) const
{
    if (!isValidChannel(channel) || on_count == nullptr || off_count == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureReady();
    if (err != ESP_OK) {
        return err;
    }

    uint8_t frame[4] = {};
    err = i2c.regRead(address_, ledBaseRegister(channel), frame, sizeof(frame), port_);
    if (err != ESP_OK) {
        return err;
    }

    *on_count = static_cast<uint16_t>(frame[0] | ((frame[1] & 0x0F) << 8));
    *off_count = static_cast<uint16_t>(frame[2] | ((frame[3] & 0x0F) << 8));
    if (full_on != nullptr) {
        *full_on = (frame[1] & 0x10) != 0;
    }
    if (full_off != nullptr) {
        *full_off = (frame[3] & 0x10) != 0;
    }

    return ESP_OK;
}

esp_err_t Pca9685::mode1(uint8_t *value) const
{
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureReady();
    if (err != ESP_OK) {
        return err;
    }

    return i2c.regRead8(address_, kRegMode1, value, port_);
}

esp_err_t Pca9685::mode2(uint8_t *value) const
{
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureReady();
    if (err != ESP_OK) {
        return err;
    }

    return i2c.regRead8(address_, kRegMode2, value, port_);
}

esp_err_t Pca9685::prescale(uint8_t *value) const
{
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureReady();
    if (err != ESP_OK) {
        return err;
    }

    return i2c.regRead8(address_, kRegPreScale, value, port_);
}

uint16_t Pca9685::address(void) const
{
    return address_;
}

int Pca9685::port(void) const
{
    return port_;
}

uint16_t Pca9685::frequency(void) const
{
    return frequency_hz_;
}

uint16_t Pca9685::dutyCount(uint8_t percent)
{
    if (percent >= 100) {
        return 4095;
    }

    const double counts = (static_cast<double>(percent) * 4095.0) / 100.0;
    const long rounded = lround(counts);
    if (rounded < 0) {
        return 0;
    }
    if (rounded > 4095) {
        return 4095;
    }
    return static_cast<uint16_t>(rounded);
}

} // namespace esp_pca9685
