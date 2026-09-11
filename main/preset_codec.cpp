#include "preset_codec.hpp"
#include <cstring>

namespace bench {
namespace {
constexpr uint32_t kMagic = 0x31545350; // "PST1"
constexpr uint16_t kSchema = 1;

void put(uint8_t *&p, uint64_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i) { *p++ = static_cast<uint8_t>(value); value >>= 8; }
}

uint64_t get(const uint8_t *&p, unsigned bytes)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) { value |= static_cast<uint64_t>(*p++) << (8 * i); }
    return value;
}

uint32_t crc32(const uint8_t *p, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; ++i) {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((0U - (crc & 1U)) & 0xedb88320U);
        }
    }
    return ~crc;
}
} // namespace

bool validPresetName(const char *name, size_t len)
{
    if (name == nullptr || len == 0 || len > kPresetNameMaxLen) { return false; }
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < 0x20 || c > 0x7E) { return false; }
    }
    return true;
}

esp_err_t encodePreset(const char *name, size_t name_len, const esp_pixels_composer::AtScene &scene,
                       uint64_t sequence, PresetRecord &record)
{
    if (sequence == 0 || !validPresetName(name, name_len)) { return ESP_ERR_INVALID_ARG; }
    SceneRecord inner;
    const esp_err_t err = encodeScene(scene, sequence, inner);
    if (err != ESP_OK) { return err; }
    record.size = kPresetHeaderBytes + inner.size + 4;
    if (record.size > sizeof(record.bytes)) { return ESP_ERR_INVALID_SIZE; } // defensive; cannot happen

    uint8_t *p = record.bytes;
    put(p, kMagic, 4); put(p, kSchema, 2); put(p, record.size, 2); put(p, sequence, 8);
    put(p, name_len, 1);
    uint8_t name_field[kPresetNameFieldBytes] = {};
    std::memcpy(name_field, name, name_len);
    std::memcpy(p, name_field, kPresetNameFieldBytes); p += kPresetNameFieldBytes;
    std::memcpy(p, inner.bytes, inner.size); p += inner.size;
    put(p, crc32(record.bytes, record.size - 4), 4);
    return ESP_OK;
}

esp_err_t decodePreset(const PresetRecord &record, char *name_out, size_t &name_len_out,
                       esp_pixels_composer::AtScene &scene, uint64_t &sequence)
{
    if (record.size < kPresetHeaderBytes + kSceneHeaderBytes + 8 || record.size > sizeof(record.bytes)) {
        return ESP_ERR_INVALID_SIZE;
    }
    const uint8_t *crc_p = record.bytes + record.size - 4;
    if (get(crc_p, 4) != crc32(record.bytes, record.size - 4)) { return ESP_ERR_INVALID_CRC; }

    const uint8_t *p = record.bytes;
    if (get(p, 4) != kMagic || get(p, 2) != kSchema || get(p, 2) != record.size) {
        return ESP_ERR_INVALID_VERSION;
    }
    const uint64_t generation = get(p, 8);
    const uint8_t name_len = static_cast<uint8_t>(get(p, 1));
    if (generation == 0 || name_len == 0 || name_len > kPresetNameMaxLen) { return ESP_ERR_INVALID_ARG; }
    const uint8_t *name_field = p;
    p += kPresetNameFieldBytes;
    for (size_t i = name_len; i < kPresetNameFieldBytes; ++i) {
        if (name_field[i] != 0) { return ESP_ERR_INVALID_ARG; } // padding must be zero
    }
    if (!validPresetName(reinterpret_cast<const char *>(name_field), name_len)) {
        return ESP_ERR_INVALID_ARG;
    }

    SceneRecord inner;
    inner.size = record.size - kPresetHeaderBytes - 4;
    if (inner.size > sizeof(inner.bytes)) { return ESP_ERR_INVALID_SIZE; }
    std::memcpy(inner.bytes, p, inner.size);
    // Cross-check generations before decodeScene can commit caller outputs.
    const uint8_t *inner_sequence = inner.bytes + 8;
    if (get(inner_sequence, 8) != generation) { return ESP_ERR_INVALID_ARG; }
    uint64_t inner_generation = 0;
    const esp_err_t err = decodeScene(inner, scene, inner_generation);
    if (err != ESP_OK) { return err; }

    std::memcpy(name_out, name_field, name_len);
    name_out[name_len] = '\0';
    name_len_out = name_len;
    sequence = generation;
    return ESP_OK;
}
} // namespace bench
