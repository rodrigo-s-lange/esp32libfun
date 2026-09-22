#pragma once

#include "scene_codec.hpp"
#include <cstddef>
#include <cstdint>

namespace bench {

constexpr size_t kPresetNameMaxLen = 31;     ///< bytes, excludes any terminator
constexpr size_t kPresetNameFieldBytes = 32; ///< fixed on-disk field (zero-padded beyond the real name)
constexpr size_t kPresetHeaderBytes =
    4 /*magic*/ + 2 /*schema*/ + 2 /*size*/ + 8 /*generation*/ + 1 /*name_len*/ + kPresetNameFieldBytes; // 49
/// Maximum total record size: header + a maximum-sized scene_codec record + a trailing CRC32.
/// At kMaxOutputs==2, MAX_SEGMENTS==32: 49 + 1295 + 4 = 1348 (schema-3/4's
/// own shared worst case -- see `kSceneRecordBytes`'s own doc). This preset
/// wrapper's own `kSchema` (preset_codec.cpp) is an independent format
/// version for the OUTER record only; it does not change with the INNER
/// scene's own schema number, since the inner bytes are copied through
/// opaquely (see `encodePreset()`/`decodePreset()`) -- schema 4's own
/// flags-byte reinterpretation (Task 7G) needed no change here, and this
/// maximum is unchanged from the prior (schema-3) delivery.
constexpr size_t kPresetRecordBytes = kPresetHeaderBytes + kSceneRecordBytes + 4;

struct PresetRecord {
    uint8_t bytes[kPresetRecordBytes]{};
    size_t size = 0;
};

/// True for 1..kPresetNameMaxLen printable ASCII bytes (0x20-0x7E, so no
/// control characters, no raw newline/tab); comparison elsewhere is exact,
/// case-sensitive byte match, never a locale-aware collation.
/// @param name Caller-owned buffer; not required to be nul-terminated.
/// @param len Number of bytes in `name` to validate.
/// @return true iff every documented rule is satisfied.
bool validPresetName(const char *name, size_t len);

/// Encodes one named preset: an explicit little-endian header (magic,
/// schema, self-describing size, a 64-bit generation, the name, zero-padded)
/// wrapping the existing scene_codec's own complete, self-validating
/// `SceneRecord` verbatim (same `sequence` for both, same `topology` --
/// see `encodeScene()`'s own doc), plus a trailing CRC32 over the whole
/// record -- the inner scene's own CRC alone would not catch a corrupted
/// name/header. `record.size` varies with both the scene's own segment
/// count and `topology.count`, the same discipline `encodeScene()` itself
/// already has; no NVS access.
/// @param name Preset name; validated by `validPresetName()`.
/// @param name_len Number of bytes in `name`.
/// @param scene Source scene (1D and 2D segments may be freely mixed;
///        every segment validated against `map`'s own canvas, or
///        `topology.totalPixels()` under the identity bypass).
/// @param topology The device's own active topology at save time -- see
///        `encodeScene()`'s own doc; saving a preset always uses the
///        ACTIVE topology, never an unpublished/staged output configuration.
/// @param map The device's own active canvas/ledmap at save time -- see
///        `encodeScene()`'s own doc; saving a preset always uses the
///        ACTIVE map, never an unpublished/staged `AT+MAPSET` candidate.
/// @param sequence Monotonic per-position generation; zero is reserved.
/// @param record Receives the versioned, CRC-protected bytes.
/// @return ESP_OK, or an `encodeScene()`/name validation error.
esp_err_t encodePreset(const char *name, size_t name_len, const esp_pixels_composer::AtScene &scene,
                       const Topology &topology, const MapConfig &map, uint64_t sequence, PresetRecord &record);

/// Validates every byte/field -- magic/schema/size, the outer CRC32, zero
/// padding beyond the declared name length, a valid name, the embedded
/// scene_codec record's own full validation (including its topology
/// compatibility against `current_topology`, see `decodeScene()`'s own
/// doc), and that the embedded record's own generation matches this
/// record's outer one -- before changing any output; no NVS access.
/// @param record Complete record including its trailing CRC.
/// @param current_topology The device's own active topology right now --
///        loading a preset must never unexpectedly reassign physical pins,
///        so an incompatible preset (see `isCompatiblePrefix()`) is
///        rejected here, before anything changes.
/// @param current_map The device's own active canvas/ledmap right now --
///        loading a preset must never unexpectedly change the active map
///        either, so an incompatible preset (see `isCompatibleMap()`) is
///        rejected here too, before anything changes.
/// @param name_out Receives the nul-terminated name on success only; must
///        have room for at least `kPresetNameMaxLen + 1` bytes.
/// @param name_len_out Receives the name length (excluding the terminator)
///        on success only.
/// @param scene Receives the decoded scene on success only.
/// @param sequence Receives the generation on success only.
/// @return ESP_OK, or an invalid/incompatible/corrupt data error.
esp_err_t decodePreset(const PresetRecord &record, const Topology &current_topology, const MapConfig &current_map,
                       char *name_out, size_t &name_len_out, esp_pixels_composer::AtScene &scene,
                       uint64_t &sequence);

/// Validates stored content for cataloging, regardless of the current device topology.
/// @param record Complete stored bytes.
/// @param name_out Receives the name on success (at least kPresetNameMaxLen+1 bytes).
/// @param name_len_out Receives the name length on success.
/// @param scene Receives the structurally valid scene, not permission to apply it.
/// @param sequence Receives its generation on success.
/// @return ESP_OK or validation error; use decodePreset before applying a scene.
esp_err_t decodeStoredPreset(const PresetRecord &record, char *name_out, size_t &name_len_out,
                             esp_pixels_composer::AtScene &scene, uint64_t &sequence);

} // namespace bench
