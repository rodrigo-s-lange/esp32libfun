#include "scene_codec.hpp"
#include <cstring>

namespace bench {
namespace {
namespace comp = esp_pixels_composer;
namespace fx = esp_pixels_fx;
constexpr uint32_t kMagic = 0x314e4353; // SCN1
constexpr uint16_t kSchema = 1;
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

bool valid(const comp::SegmentSpec &s)
{
    const fx::FxInfo *info = fx::describe(s.effect_id);
    return !s.is2D && s.id != 0 && s.start < s.stop && s.stop <= kPixelCount &&
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

bool decodeSegment(const uint8_t *&p, comp::SegmentSpec &s)
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
    return reversed <= 1 && valid(s);
}
} // namespace

esp_err_t encodeScene(const comp::AtScene &scene, uint64_t sequence, SceneRecord &record)
{
    if (sequence == 0 || scene.count > comp::MAX_SEGMENTS) { return ESP_ERR_INVALID_ARG; }
    for (uint8_t i = 0; i < scene.count; ++i) {
        if (!valid(scene.segments[i])) { return ESP_ERR_INVALID_ARG; }
        for (uint8_t j = 0; j < i; ++j) {
            if (scene.segments[j].id == scene.segments[i].id) { return ESP_ERR_INVALID_ARG; }
        }
    }
    record.size = kSceneHeaderBytes + scene.count * kSceneSegmentBytes + 4;
    uint8_t *p = record.bytes;
    put(p, kMagic, 4); put(p, kSchema, 2); put(p, record.size, 2); put(p, sequence, 8);
    put(p, kPixelPin, 2); put(p, kPixelCount, 2);
    put(p, 1, 1); put(p, 0, 1); // Wire ABI: 1=GRB, RGB (not RGBW).
    put(p, scene.count, 1); put(p, scene.brightness, 1); put(p, scene.paused, 1);
    put(p, 0, 3); put(p, kFxCatalog, 4);
    for (uint8_t i = 0; i < scene.count; ++i) { encodeSegment(p, scene.segments[i]); }
    put(p, crc32(record.bytes, record.size - 4), 4);
    return ESP_OK;
}

esp_err_t decodeScene(const SceneRecord &record, comp::AtScene &scene, uint64_t &sequence)
{
    if (record.size < kSceneHeaderBytes + 4 || record.size > kSceneRecordBytes) {
        return ESP_ERR_INVALID_SIZE;
    }
    const uint8_t *crc = record.bytes + record.size - 4;
    if (get(crc, 4) != crc32(record.bytes, record.size - 4)) { return ESP_ERR_INVALID_CRC; }
    const uint8_t *p = record.bytes;
    if (get(p, 4) != kMagic || get(p, 2) != kSchema || get(p, 2) != record.size) {
        return ESP_ERR_INVALID_VERSION;
    }
    const uint64_t generation = get(p, 8);
    if (generation == 0 || get(p, 2) != kPixelPin || get(p, 2) != kPixelCount ||
        get(p, 1) != 1 || get(p, 1) != 0) { return ESP_ERR_INVALID_ARG; }
    const uint8_t count = get(p, 1), brightness = get(p, 1), paused = get(p, 1);
    if (count > comp::MAX_SEGMENTS || paused > 1 || get(p, 3) != 0 ||
        get(p, 4) != kFxCatalog || record.size != kSceneHeaderBytes + count * kSceneSegmentBytes + 4) {
        return ESP_ERR_INVALID_ARG;
    }
    // First pass validates every entry and duplicate ID, without mutating outputs.
    for (uint8_t i = 0; i < count; ++i) {
        comp::SegmentSpec spec;
        if (!decodeSegment(p, spec)) { return ESP_ERR_INVALID_ARG; }
        for (uint8_t j = 0; j < i; ++j) {
            const uint8_t *prior = record.bytes + kSceneHeaderBytes + j * kSceneSegmentBytes;
            if (get(prior, 4) == spec.id) { return ESP_ERR_INVALID_ARG; }
        }
    }
    p = record.bytes + kSceneHeaderBytes;
    for (uint8_t i = 0; i < count; ++i) { (void)decodeSegment(p, scene.segments[i]); }
    scene.count = count; scene.brightness = brightness; scene.paused = paused != 0;
    sequence = generation;
    return ESP_OK;
}
} // namespace bench
