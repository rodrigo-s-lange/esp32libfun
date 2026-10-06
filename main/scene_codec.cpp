#include "scene_codec.hpp"
#include <cstring>

namespace bench {
namespace {
namespace comp = esp_pixels_composer;
namespace fx = esp_pixels_fx;
constexpr uint32_t kMagic = 0x314e4353; // SCN1 -- shared by all five schemas
constexpr uint16_t kSchemaV1 = 1;       // published bench format: one fixed GPIO16/50/GRB output; read-only from here
constexpr uint16_t kSchemaV2 = 2;       // prior delivery's format: 1..kMaxOutputs configured outputs, no map/2D; read-only from here
constexpr uint16_t kSchemaV3 = 3;       // prior delivery's format: adds map/2D segments; read-only from here
constexpr uint16_t kSchemaV4 = 4;       // prior format: schema 3's reverse byte becomes flags (bit0 reverse, bit1 mirror)
constexpr uint16_t kSchemaV5 = 5;       // adds map wiring code 2 (COLUMN_SERPENTINE); layout unchanged
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

// Shape-aware validity: 1D and 2D specs use the same discriminator/rules
// Composer::applyConfig() itself enforces (Design v2 -- h==1 is 1D-shaped
// regardless of is2D, a 2D spec never carries reversed). `canvas_width`/
// `canvas_height` are the record's OWN canvas (identity: totalPixels()/1;
// real map: map.width/map.height) -- never the current device's.
bool valid(const comp::SegmentSpec &s, uint16_t canvas_width, uint16_t canvas_height)
{
    if (s.id == 0 || static_cast<unsigned>(s.blend) > static_cast<unsigned>(comp::BlendMode::Alpha) ||
        static_cast<unsigned>(s.state) > static_cast<unsigned>(comp::SegmentState::Disabled) ||
        s.params.palette >= fx::builtinPaletteCount() ||
        static_cast<unsigned>(s.params.color_source) > fx::FX_COLOR_SOURCE_COLORS) {
        return false;
    }
    const fx::FxInfo *info = fx::describe(s.effect_id);
    if (info == nullptr) {
        return false;
    }
    if (s.is2D) {
        if (s.reversed || s.mirror || s.w == 0 || s.h == 0 ||
            static_cast<uint32_t>(s.x) + s.w > canvas_width || static_cast<uint32_t>(s.y) + s.h > canvas_height) {
            return false;
        }
        const uint8_t required_dim = (s.h > 1) ? fx::FX_DIM_2D : fx::FX_DIM_1D;
        return (info->dim & required_dim) != 0;
    }
    const uint32_t canvas_cells = static_cast<uint32_t>(canvas_width) * canvas_height;
    return s.start < s.stop && s.stop <= canvas_cells && (info->dim & fx::FX_DIM_1D) != 0;
}

// Schema 4's own per-segment layout: byte-for-byte identical to schema 3's
// (same kSceneSegmentBytesV3 == 39, same field positions) except the ONE
// byte at this exact position, which schema 3 wrote as a strict 0/1
// `reversed` value -- schema 4 instead writes a flags byte (bit0 reversed,
// bit1 mirror, Task 7G). This is the ONLY encoder now used (encodeScene()
// always writes the latest schema); decodeSegment() below still reads the
// OLD strict schema-3 meaning for a schema-3 record, unchanged, and
// decodeSegmentV4() reads this new flags meaning for a schema-4 one -- the
// two are never conflated, and no old record's own byte is ever
// reinterpreted in place.
void encodeSegment(uint8_t *&p, const comp::SegmentSpec &s)
{
    put(p, s.id, 4);
    put(p, s.is2D ? s.x : s.start, 2);
    put(p, s.is2D ? s.y : s.stop, 2);
    put(p, s.effect_id, 1); put(p, s.brightness, 1);
    put(p, static_cast<uint8_t>(s.blend), 1); put(p, s.opacity, 1);
    const uint8_t flags = (s.reversed ? 1U : 0U) | (s.mirror ? 2U : 0U);
    put(p, flags, 1); put(p, static_cast<uint8_t>(s.state), 1);
    put(p, s.restart_generation, 4);
    for (const auto &color : s.params.color) {
        put(p, color.r, 1); put(p, color.g, 1); put(p, color.b, 1);
    }
    put(p, s.params.speed, 1); put(p, s.params.intensity, 1);
    put(p, s.params.palette, 1); put(p, s.params.color_source, 1);
    for (uint8_t custom : s.params.custom) { put(p, custom, 1); }
    put(p, s.is2D, 1);
    put(p, s.is2D ? s.w : 0, 2);
    put(p, s.is2D ? s.h : 0, 2);
}

// Schema 3's own reader: read-only from here on (nothing encodes this
// layout anymore) -- strictly validates the old 0/1-only `reversed` byte
// meaning, exactly as schema 3 originally defined it, and never sets
// `mirror` (a schema-3 record predates that field entirely; `s.mirror`
// stays `false`, its own default from `s = comp::SegmentSpec{}` below).
bool decodeSegment(const uint8_t *&p, comp::SegmentSpec &s, uint16_t canvas_width, uint16_t canvas_height)
{
    s = comp::SegmentSpec{};
    const uint32_t a = static_cast<uint32_t>(get(p, 4));
    const uint16_t field1 = static_cast<uint16_t>(get(p, 2));
    const uint16_t field2 = static_cast<uint16_t>(get(p, 2));
    s.id = a;
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
    const uint8_t is2D = get(p, 1);
    const uint16_t w = static_cast<uint16_t>(get(p, 2));
    const uint16_t h = static_cast<uint16_t>(get(p, 2));
    if (is2D > 1 || reversed > 1) { return false; }
    s.is2D = is2D != 0;
    if (s.is2D) {
        s.x = field1; s.y = field2; s.w = w; s.h = h;
    } else {
        s.start = field1; s.stop = field2;
        if (w != 0 || h != 0) { return false; } // "only the active geometry fields" -- the inactive pair must be zero
    }
    return valid(s, canvas_width, canvas_height);
}

// Schema 4's own reader (Task 7G): byte-for-byte identical to
// decodeSegment() above except the flags byte -- bit0 reversed, bit1
// mirror, any other bit set is rejected outright (an unknown flag must
// never be silently ignored). Never reinterprets a schema-3 record's own
// strict 0/1 reversed byte as flags; schema 3 keeps reading through
// decodeSegment() above, unchanged.
bool decodeSegmentV4(const uint8_t *&p, comp::SegmentSpec &s, uint16_t canvas_width, uint16_t canvas_height)
{
    s = comp::SegmentSpec{};
    const uint32_t a = static_cast<uint32_t>(get(p, 4));
    const uint16_t field1 = static_cast<uint16_t>(get(p, 2));
    const uint16_t field2 = static_cast<uint16_t>(get(p, 2));
    s.id = a;
    s.effect_id = get(p, 1); s.brightness = get(p, 1);
    s.blend = static_cast<comp::BlendMode>(get(p, 1)); s.opacity = get(p, 1);
    const uint8_t flags = get(p, 1);
    s.reversed = (flags & 1U) != 0; s.mirror = (flags & 2U) != 0;
    s.state = static_cast<comp::SegmentState>(get(p, 1));
    s.restart_generation = get(p, 4);
    for (auto &color : s.params.color) {
        color.r = get(p, 1); color.g = get(p, 1); color.b = get(p, 1);
    }
    s.params.speed = get(p, 1); s.params.intensity = get(p, 1);
    s.params.palette = get(p, 1); s.params.color_source = static_cast<fx::FxColorSource>(get(p, 1));
    for (uint8_t &custom : s.params.custom) { custom = get(p, 1); }
    const uint8_t is2D = get(p, 1);
    const uint16_t w = static_cast<uint16_t>(get(p, 2));
    const uint16_t h = static_cast<uint16_t>(get(p, 2));
    if (is2D > 1 || flags > 3U) { return false; } // flags > 3: an unknown bit set -- reject, never ignore
    s.is2D = is2D != 0;
    if (s.is2D) {
        s.x = field1; s.y = field2; s.w = w; s.h = h;
    } else {
        s.start = field1; s.stop = field2;
        if (w != 0 || h != 0) { return false; } // "only the active geometry fields" -- the inactive pair must be zero
    }
    return valid(s, canvas_width, canvas_height);
}

// Schema-1/2 segments have no is2D/w/h fields at all -- always 1D, decoded
// with the OLD fixed 34-byte layout, validated against the identity canvas
// (canvas_height == 1) exactly as before this delivery. Read-only: nothing
// ever encodes this layout anymore (encodeScene() always writes schema 3),
// so there is no matching encodeSegmentV2().
bool decodeSegmentV2(const uint8_t *&p, comp::SegmentSpec &s, uint16_t total_pixels)
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
    return reversed <= 1 && valid(s, total_pixels, 1);
}
} // namespace

esp_err_t encodeScene(const comp::AtScene &scene, const Topology &topology, const MapConfig &map,
                      uint64_t sequence, SceneRecord &record)
{
    if (sequence == 0 || scene.count > comp::MAX_SEGMENTS || !topology.valid() ||
        !map.valid(topology.totalPixels())) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint16_t canvas_width = map.isIdentity() ? topology.totalPixels() : map.width;
    const uint16_t canvas_height = map.isIdentity() ? 1 : map.height;
    for (uint8_t i = 0; i < scene.count; ++i) {
        if (!valid(scene.segments[i], canvas_width, canvas_height)) { return ESP_ERR_INVALID_ARG; }
        for (uint8_t j = 0; j < i; ++j) {
            if (scene.segments[j].id == scene.segments[i].id) { return ESP_ERR_INVALID_ARG; }
        }
    }
    const size_t header_bytes =
        4 + 2 + 2 + 8 + 1 + static_cast<size_t>(topology.count) * 5 + 7 + 1 + 1 + 1 + 2 + 4;
    record.size = header_bytes + scene.count * kSceneSegmentBytesV3 + 4;
    if (record.size > sizeof(record.bytes)) { return ESP_ERR_INVALID_SIZE; } // defensive; cannot happen
    uint8_t *p = record.bytes;
    put(p, kMagic, 4); put(p, kSchemaV5, 2); put(p, record.size, 2); put(p, sequence, 8);
    put(p, topology.count, 1);
    for (uint8_t i = 0; i < topology.count; ++i) {
        const OutputSpec &o = topology.outputs[i];
        put(p, static_cast<uint16_t>(o.pin), 2); put(p, o.count, 2); put(p, o.order, 1);
    }
    put(p, map.width, 2); put(p, map.height, 2); put(p, map.first, 2); put(p, static_cast<uint8_t>(map.wiring), 1);
    put(p, scene.count, 1); put(p, scene.brightness, 1); put(p, scene.paused, 1);
    put(p, 0, 2); put(p, kFxCatalog, 4);
    for (uint8_t i = 0; i < scene.count; ++i) { encodeSegment(p, scene.segments[i]); }
    put(p, crc32(record.bytes, record.size - 4), 4);
    return ESP_OK;
}

static esp_err_t decodeSceneChecked(const SceneRecord &record, const Topology *current_topology,
                                    const MapConfig *current_map, comp::AtScene &scene, uint64_t &sequence)
{
    if ((current_topology != nullptr && (!current_topology->valid() ||
         (current_map != nullptr && !current_map->valid(current_topology->totalPixels())))) ||
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
    MapConfig record_map{}; // identity by default -- schema 1/2 have no map field at all
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
    } else if (schema == kSchemaV3 || schema == kSchemaV4 || schema == kSchemaV5) {
        // Schemas 3/4/5 share an IDENTICAL header layout (topology tuples,
        // then the map descriptor). Schema 5 extends the stable wiring enum;
        // segment flags retain schema 4's meaning.
        if (record.size < 17 + 4) { return ESP_ERR_INVALID_SIZE; }
        const uint8_t output_count = static_cast<uint8_t>(get(p, 1));
        if (output_count == 0 || output_count > kMaxOutputs) { return ESP_ERR_INVALID_ARG; }
        if (record.size < 26 + static_cast<size_t>(output_count) * 5 + 7 + 4) { return ESP_ERR_INVALID_SIZE; }
        record_topology.count = output_count;
        for (uint8_t i = 0; i < output_count; ++i) {
            const uint16_t pin = static_cast<uint16_t>(get(p, 2));
            const uint16_t count = static_cast<uint16_t>(get(p, 2));
            const uint8_t order = static_cast<uint8_t>(get(p, 1));
            record_topology.outputs[i] = OutputSpec{static_cast<int16_t>(pin), count, order};
        }
        record_map.width = static_cast<uint16_t>(get(p, 2));
        record_map.height = static_cast<uint16_t>(get(p, 2));
        record_map.first = static_cast<uint16_t>(get(p, 2));
        const uint8_t wiring = static_cast<uint8_t>(get(p, 1));
        if (wiring > (schema == kSchemaV5 ? 2 : 1)) { return ESP_ERR_INVALID_ARG; }
        record_map.wiring = static_cast<MapWiring>(wiring);
    } else {
        return ESP_ERR_INVALID_VERSION;
    }
    if (!record_topology.valid() || !record_map.valid(record_topology.totalPixels())) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t count = static_cast<uint8_t>(get(p, 1));
    const uint8_t brightness = static_cast<uint8_t>(get(p, 1));
    const uint8_t paused = static_cast<uint8_t>(get(p, 1));
    const unsigned reserved_bytes = (schema == kSchemaV1) ? 3 : 2;
    if (count > comp::MAX_SEGMENTS || paused > 1 || get(p, reserved_bytes) != 0 || get(p, 4) != kFxCatalog) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t header_bytes = static_cast<size_t>(p - record.bytes);
    const size_t segment_bytes =
        (schema == kSchemaV3 || schema == kSchemaV4 || schema == kSchemaV5)
            ? kSceneSegmentBytesV3 : kSceneSegmentBytes;
    if (record.size != header_bytes + count * segment_bytes + 4) {
        return ESP_ERR_INVALID_ARG;
    }

    // A stored record must be valid on its own, including its original bounds.
    const uint16_t canvas_width = record_map.isIdentity() ? record_topology.totalPixels() : record_map.width;
    const uint16_t canvas_height = record_map.isIdentity() ? 1 : record_map.height;

    // First pass validates every entry and duplicate ID, without mutating outputs.
    bool has_2d = false;
    for (uint8_t i = 0; i < count; ++i) {
        comp::SegmentSpec spec;
        const bool ok = (schema == kSchemaV4 || schema == kSchemaV5)
                                                    ? decodeSegmentV4(p, spec, canvas_width, canvas_height)
                        : (schema == kSchemaV3) ? decodeSegment(p, spec, canvas_width, canvas_height)
                                                : decodeSegmentV2(p, spec, canvas_width);
        if (!ok) { return ESP_ERR_INVALID_ARG; }
        has_2d = has_2d || spec.is2D;
        for (uint8_t j = 0; j < i; ++j) {
            const uint8_t *prior = record.bytes + header_bytes + j * segment_bytes;
            if (get(prior, 4) == spec.id) { return ESP_ERR_INVALID_ARG; }
        }
    }
    if (current_topology != nullptr && !isCompatiblePrefix(record_topology, *current_topology)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (current_map != nullptr && !isCompatibleMap(record_map, *current_map,
            has_2d ? 0 : record_topology.totalPixels())) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    p = record.bytes + header_bytes;
    for (uint8_t i = 0; i < count; ++i) {
        if (schema == kSchemaV4 || schema == kSchemaV5) {
            (void)decodeSegmentV4(p, scene.segments[i], canvas_width, canvas_height);
        } else if (schema == kSchemaV3) {
            (void)decodeSegment(p, scene.segments[i], canvas_width, canvas_height);
        } else {
            (void)decodeSegmentV2(p, scene.segments[i], canvas_width);
        }
    }
    scene.count = count; scene.brightness = brightness; scene.paused = paused != 0;
    sequence = generation;
    return ESP_OK;
}

esp_err_t decodeScene(const SceneRecord &record, const Topology &current_topology, const MapConfig &current_map,
                      comp::AtScene &scene, uint64_t &sequence)
{
    return decodeSceneChecked(record, &current_topology, &current_map, scene, sequence);
}

esp_err_t decodeStoredScene(const SceneRecord &record, comp::AtScene &scene, uint64_t &sequence)
{
    return decodeSceneChecked(record, nullptr, nullptr, scene, sequence);
}
} // namespace bench
