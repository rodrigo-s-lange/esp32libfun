#include "output_store.hpp"
#include "esp_log.h"
#include "nvs_flash.h"
#include <cstring>

namespace bench {
namespace {
constexpr const char *kKeys[] = {"outputs_a", "outputs_b"};
constexpr const char *kTag = "OUTPUT_NVS";
constexpr uint32_t kMagic = 0x324e4f43; // "CON2" (reads distinctly from scene_codec's "SCN1"/preset_codec's "PST1")
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

esp_err_t encodeTopology(const Topology &topology, uint64_t sequence, OutputStore::Record &record)
{
    if (sequence == 0 || !topology.valid()) { return ESP_ERR_INVALID_ARG; }
    record.size = 4 + 2 + 2 + 8 + 1 + static_cast<size_t>(topology.count) * 5 + 4;
    uint8_t *p = record.bytes;
    put(p, kMagic, 4); put(p, kSchema, 2); put(p, record.size, 2); put(p, sequence, 8);
    put(p, topology.count, 1);
    for (uint8_t i = 0; i < topology.count; ++i) {
        const OutputSpec &o = topology.outputs[i];
        put(p, static_cast<uint16_t>(o.pin), 2); put(p, o.count, 2); put(p, o.order, 1);
    }
    put(p, crc32(record.bytes, record.size - 4), 4);
    return ESP_OK;
}

esp_err_t decodeTopology(const OutputStore::Record &record, Topology &topology, uint64_t &sequence)
{
    if (record.size < 4 + 2 + 2 + 8 + 1 + 5 + 4 || record.size > sizeof(record.bytes)) {
        return ESP_ERR_INVALID_SIZE;
    }
    const uint8_t *crc = record.bytes + record.size - 4;
    if (get(crc, 4) != crc32(record.bytes, record.size - 4)) { return ESP_ERR_INVALID_CRC; }
    const uint8_t *p = record.bytes;
    if (get(p, 4) != kMagic || get(p, 2) != kSchema || get(p, 2) != record.size) {
        return ESP_ERR_INVALID_VERSION;
    }
    const uint64_t generation = get(p, 8);
    const uint8_t count = static_cast<uint8_t>(get(p, 1));
    if (generation == 0 || count == 0 || count > kMaxOutputs) { return ESP_ERR_INVALID_ARG; }
    if (record.size != 4 + 2 + 2 + 8 + 1 + static_cast<size_t>(count) * 5 + 4) { return ESP_ERR_INVALID_ARG; }
    Topology candidate{};
    candidate.count = count;
    for (uint8_t i = 0; i < count; ++i) {
        const uint16_t pin = static_cast<uint16_t>(get(p, 2));
        const uint16_t pixel_count = static_cast<uint16_t>(get(p, 2));
        const uint8_t order = static_cast<uint8_t>(get(p, 1));
        candidate.outputs[i] = OutputSpec{static_cast<int16_t>(pin), pixel_count, order};
    }
    if (!candidate.valid()) { return ESP_ERR_INVALID_ARG; }
    topology = candidate;
    sequence = generation;
    return ESP_OK;
}
} // namespace

esp_err_t OutputStore::load(Topology &topology)
{
    if (handle_ != 0) { return ESP_ERR_INVALID_STATE; }
    init_error_ = nvs_flash_init();
    if (init_error_ != ESP_OK) { return init_error_; }
    init_error_ = nvs_open("pixels_outputs", NVS_READWRITE, &handle_);
    if (init_error_ != ESP_OK) { return init_error_; }
    esp_err_t last_error = ESP_ERR_NOT_FOUND;
    for (int slot = 0; slot < 2; ++slot) {
        scratch_.size = sizeof(scratch_.bytes);
        esp_err_t err = nvs_get_blob(handle_, kKeys[slot], scratch_.bytes, &scratch_.size);
        if (err == ESP_ERR_NVS_NOT_FOUND) { continue; }
        if (err != ESP_OK) {
            last_error = err;
            ESP_LOGW(kTag, "slot %d unreadable: %s", slot, esp_err_to_name(err));
            continue;
        }
        uint64_t candidate_sequence = 0;
        if (scratch_.size >= 16) {
            for (unsigned i = 0; i < 8; ++i) {
                candidate_sequence |= static_cast<uint64_t>(scratch_.bytes[8 + i]) << (8 * i);
            }
        }
        if (active_slot_ >= 0 && candidate_sequence <= sequence_) { continue; }
        Topology candidate{};
        err = decodeTopology(scratch_, candidate, candidate_sequence);
        if (err != ESP_OK) {
            last_error = err;
            ESP_LOGW(kTag, "slot %d rejected: %s", slot, esp_err_to_name(err));
            continue;
        }
        topology = candidate;
        sequence_ = candidate_sequence;
        active_slot_ = slot;
        cached_ = scratch_;
    }
    if (active_slot_ < 0) { return last_error; }
    current_ = topology;
    ESP_LOGI(kTag, "restored slot=%d generation=%llu outputs=%u", active_slot_,
             static_cast<unsigned long long>(sequence_), static_cast<unsigned>(topology.count));
    return ESP_OK;
}

esp_err_t OutputStore::save(const Topology &topology)
{
    if (init_error_ != ESP_OK) { return init_error_; }
    if (handle_ == 0 || sequence_ == UINT64_MAX) { return ESP_ERR_INVALID_STATE; }
    esp_err_t err = encodeTopology(topology, sequence_ + 1, scratch_);
    if (err != ESP_OK) { return err; }
    if (active_slot_ >= 0 && cached_.size == scratch_.size &&
        std::memcmp(cached_.bytes, scratch_.bytes, 8) == 0 &&
        std::memcmp(cached_.bytes + 16, scratch_.bytes + 16, scratch_.size - 20) == 0) {
        return ESP_OK;
    }
    const int target = active_slot_ == 0 ? 1 : 0;
    err = nvs_set_blob(handle_, kKeys[target], scratch_.bytes, scratch_.size);
    if (err == ESP_OK) { err = nvs_commit(handle_); }
    if (err != ESP_OK) { return err; }
    verify_.size = sizeof(verify_.bytes);
    err = nvs_get_blob(handle_, kKeys[target], verify_.bytes, &verify_.size);
    if (err != ESP_OK) { return err; }
    if (verify_.size != scratch_.size || std::memcmp(verify_.bytes, scratch_.bytes, scratch_.size) != 0) {
        return ESP_ERR_INVALID_CRC;
    }
    cached_ = scratch_;
    active_slot_ = target;
    ++sequence_;
    current_ = topology;
    ESP_LOGI(kTag, "saved slot=%d generation=%llu bytes=%u outputs=%u", active_slot_,
             static_cast<unsigned long long>(sequence_), static_cast<unsigned>(cached_.size),
             static_cast<unsigned>(topology.count));
    return ESP_OK;
}
} // namespace bench
