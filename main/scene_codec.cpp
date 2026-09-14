#include "scene_codec.hpp"
#include <cstring>

namespace bench {
namespace {
namespace comp = esp_pixels_composer;
namespace fx = esp_pixels_fx;
constexpr uint32_t kMagic = 0x314e4353; // SCN1 -- shared by both schemas
constexpr uint16_t kSchemaV1 = 1;       // published bench format: one fixed GPIO16/50/GRB output; read-only from here
constexpr uint16_t kSchemaV2 = 2;       // this delivery's format: 1..kMaxOutputs configured outputs
// Semantic IDs/parameter meanings, not the C++ ABI. Bump on incompatible FX changes.
constexpr uint32_t kFxCatalog = 0x00020000;

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

bool valid(const comp::SegmentSpec &s, uint16_t total_pixels)
{
    const fx::FxInfo *info = fx::describe(s.effect_id);
    return !s.is2D && s.id != 0 && s.start < s.stop && s.stop <= total_pixels &&
        static_cast<unsigned>(s.blend) <= static_cast<unsigned>(comp::BlendMode::Alpha) &&
        static_cast<unsigned>(s.state) <= static_cast<unsigned>(comp::SegmentState::Disabled) &&
        s.params.palette < fx::builtinPaletteCount() &&
        static_cast<unsigned>(s.params.color_source) <= fx::FX_COLOR_SOURCE_COLORS &&
        info != nullptr && (info->dim & fx::FX_DIM_1D) != 0 &&
        info->work.fixed_bytes == 0 && info->work.bytes_per_cell == 0;
}

void encodeSegment(uint8_t *&p, const comp::SegmentSpec &s)
{
    put(p, s.id, 4); put(p, s.start, 2); put(p, s.stop, 2);
    put(p, s.effect_id, 1); put(p, s.brightness, 1);
    put(p, static_cast<uint8_t>(s.blend), 1); put(p, s.opacity, 1);
    put(p, s.reversed, 1); put(p, static_cast<uint8_t>(s.state), 1);
    put(p, s.restart_generation, 4);
    for (const auto &color : s.params.color) {
        put(p, color.r, 1); put(p, color.g, 1); put(p, color.b, 1);
    }
    put(p, s.params.speed, 1); put(p, s.params.intensity, 1);
    put(p, s.params.palette, 1); put(p, s.params.color_source, 1);
    for (uint8_t custom : s.params.custom) { put(p, custom, 1); }
}

bool decodeSegment(const uint8_t *&p, comp::SegmentSpec &s, uint16_t total_pixels)
{
    s = comp::SegmentSpec{};
    s.id = get(p, 4); s.start = get(p, 2); s.stop = get(p, 2);
    s.effect_id = get(p, 1); s.brightness = get(p, 1);
    s.blend = static_cast<comp::BlendMode>(get(p, 1)); s.opacity = get(p, 1);
    const uint8_t reversed = get(p, 1);
    s.reversed = reversed != 0; s.state = static_cast<comp::SegmentState>(get(p, 1));
    s.restart_generation = get(p, 4);
    for (auto &color : s.params.color) {
        color.r = get(p, 1); color.g = get(p, 1); color.b = get(p, 1);
    }
    s.params.speed = get(p, 1); s.params.intensity = get(p, 1);
    s.params.palette = get(p, 1); s.params.color_source = static_cast<fx::FxColorSource>(get(p, 1));
    for (uint8_t &custom : s.params.custom) { custom = get(p, 1); }
    return reversed <= 1 && valid(s, total_pixels);
}
} // namespace

esp_err_t encodeScene(const comp::AtScene &scene, const Topology &topology, uint64_t sequence, SceneRecord &record)
{
    if (sequence == 0 || scene.count > comp::MAX_SEGMENTS || !topology.valid()) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint16_t total_pixels = topology.totalPixels();
    for (uint8_t i = 0; i < scene.count; ++i) {
        if (!valid(scene.segments[i], total_pixels)) { return ESP_ERR_INVALID_ARG; }
        for (uint8_t j = 0; j < i; ++j) {
            if (scene.segments[j].id == scene.segments[i].id) { return ESP_ERR_INVALID_ARG; }
        }
    }
    const size_t header_bytes = 4 + 2 + 2 + 8 + 1 + static_cast<size_t>(topology.count) * 5 + 1 + 1 + 1 + 2 + 4;
    record.size = header_bytes + scene.count * kSceneSegmentBytes + 4;
    if (record.size > sizeof(record.bytes)) { return ESP_ERR_INVALID_SIZE; } // defensive; cannot happen
    uint8_t *p = record.bytes;
    put(p, kMagic, 4); put(p, kSchemaV2, 2); put(p, record.size, 2); put(p, sequence, 8);
    put(p, topology.count, 1);
    for (uint8_t i = 0; i < topology.count; ++i) {
        const OutputSpec &o = topology.outputs[i];
        put(p, static_cast<uint16_t>(o.pin), 2); put(p, o.count, 2); put(p, o.order, 1);
    }
    put(p, scene.count, 1); put(p, scene.brightness, 1); put(p, scene.paused, 1);
    put(p, 0, 2); put(p, kFxCatalog, 4);
    for (uint8_t i = 0; i < scene.count; ++i) { encodeSegment(p, scene.segments[i]); }
    put(p, crc32(record.bytes, record.size - 4), 4);
    return ESP_OK;
}

static esp_err_t decodeSceneChecked(const SceneRecord &record, const Topology *current_topology, comp::AtScene &scene,
                      uint64_t &sequence)
{
    if ((current_topology != nullptr && !current_topology->valid()) ||
        record.size < 20 || record.size > sizeof(record.bytes)) {
        return ESP_ERR_INVALID_SIZE;
    }
    const uint8_t *crc = record.bytes + record.size - 4;
    if (get(crc, 4) != crc32(record.bytes, record.size - 4)) { return ESP_ERR_INVALID_CRC; }
    const uint8_t *p = record.bytes;
    if (get(p, 4) != kMagic) { return ESP_ERR_INVALID_VERSION; }
    const uint16_t schema = static_cast<uint16_t>(get(p, 2));
    if (get(p, 2) != record.size) { return ESP_ERR_INVALID_ARG; }
    const uint64_t generation = get(p, 8);
    if (generation == 0) { return ESP_ERR_INVALID_ARG; }

    Topology record_topology{};
    if (schema == kSchemaV1) {
        if (record.size < kSceneHeaderBytesV1 + 4) { return ESP_ERR_INVALID_SIZE; }
        const uint16_t pin = static_cast<uint16_t>(get(p, 2));
        const uint16_t count = static_cast<uint16_t>(get(p, 2));
        const uint8_t order_code = static_cast<uint8_t>(get(p, 1)); // 1=GRB, the only value ever written
        const uint8_t rgbw = static_cast<uint8_t>(get(p, 1));       // 0=RGB, the only value ever written
        if (order_code != 1 || rgbw != 0 || pin != kDefaultPin || count != kDefaultCount) {
            return ESP_ERR_INVALID_ARG;
        }
        record_topology.count = 1;
        record_topology.outputs[0] = OutputSpec{static_cast<int16_t>(pin), count, 0}; // order_code 1 == this schema's order 0 (GRB)
    } else if (schema == kSchemaV2) {
        if (record.size < 17 + 4) { return ESP_ERR_INVALID_SIZE; }
        const uint8_t output_count = static_cast<uint8_t>(get(p, 1));
        if (output_count == 0 || output_count > kMaxOutputs) { return ESP_ERR_INVALID_ARG; }
        if (record.size < 26 + static_cast<size_t>(output_count) * 5 + 4) { return ESP_ERR_INVALID_SIZE; }
        record_topology.count = output_count;
        for (uint8_t i = 0; i < output_count; ++i) {
            const uint16_t pin = static_cast<uint16_t>(get(p, 2));
            const uint16_t count = static_cast<uint16_t>(get(p, 2));
            const uint8_t order = static_cast<uint8_t>(get(p, 1));
            record_topology.outputs[i] = OutputSpec{static_cast<int16_t>(pin), count, order};
        }
    } else {
        return ESP_ERR_INVALID_VERSION;
    }
    if (!record_topology.valid()) { return ESP_ERR_INVALID_ARG; }

    const uint8_t count = static_cast<uint8_t>(get(p, 1));
    const uint8_t brightness = static_cast<uint8_t>(get(p, 1));
    const uint8_t paused = static_cast<uint8_t>(get(p, 1));
    const unsigned reserved_bytes = (schema == kSchemaV1) ? 3 : 2;
    if (count > comp::MAX_SEGMENTS || paused > 1 || get(p, reserved_bytes) != 0 || get(p, 4) != kFxCatalog) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t header_bytes = static_cast<size_t>(p - record.bytes);
    if (record.size != header_bytes + count * kSceneSegmentBytes + 4) {
        return ESP_ERR_INVALID_ARG;
    }

    // A stored record must be valid on its own, including its original bounds.
    const uint16_t total_pixels = record_topology.totalPixels();

    // First pass validates every entry and duplicate ID, without mutating outputs.
    for (uint8_t i = 0; i < count; ++i) {
        comp::SegmentSpec spec;
        if (!decodeSegment(p, spec, total_pixels)) { return ESP_ERR_INVALID_ARG; }
        for (uint8_t j = 0; j < i; ++j) {
            const uint8_t *prior = record.bytes + header_bytes + j * kSceneSegmentBytes;
            if (get(prior, 4) == spec.id) { return ESP_ERR_INVALID_ARG; }
        }
    }
    if (current_topology != nullptr && !isCompatiblePrefix(record_topology, *current_topology)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    p = record.bytes + header_bytes;
    for (uint8_t i = 0; i < count; ++i) { (void)decodeSegment(p, scene.segments[i], total_pixels); }
    scene.count = count; scene.brightness = brightness; scene.paused = paused != 0;
    sequence = generation;
    return ESP_OK;
}

esp_err_t decodeScene(const SceneRecord &record, const Topology &current_topology,
                      comp::AtScene &scene, uint64_t &sequence)
{
    return decodeSceneChecked(record, &current_topology, scene, sequence);
}

esp_err_t decodeStoredScene(const SceneRecord &record, comp::AtScene &scene, uint64_t &sequence)
{
    return decodeSceneChecked(record, nullptr, scene, sequence);
}
} // namespace bench
