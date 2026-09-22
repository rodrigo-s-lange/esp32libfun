#pragma once

#include "esp_pixels_composer_at_scene.hpp"
#include "map_config.hpp"
#include "output_config.hpp"
#include <cstddef>
#include <cstdint>

namespace bench {

/// Schema 1/2's own fixed per-segment size (1D only: id/start/stop/effect/
/// brightness/blend/opacity/reversed/state/restart_generation/3 colors/
/// speed/intensity/palette/color_source/3 custom bytes). Read-only from
/// here on -- see `kSceneSegmentBytesV3` for schema 3/4's own layout.
constexpr size_t kSceneSegmentBytes = 34;
/// Schema 3 AND schema 4's own per-segment size (schema 4, this delivery,
/// deliberately keeps schema 3's exact byte count -- Task 7G): everything
/// schema 1/2 already had, PLUS a 1-byte `is2D` discriminator and 2D `w`/`h`
/// fields (2 bytes each) -- `start`/`stop` are REUSED to carry `x`/`y` when
/// `is2D == true` ("only the active geometry fields", never a redundant
/// second start/stop pair sitting unused alongside them). The ONE byte
/// schema 3 defined as a strict 0/1 `reversed` value becomes a schema-4
/// flags byte (bit0 reversed, bit1 mirror) at the SAME position -- schema 3
/// records keep their own old strict meaning, read unchanged; see
/// `scene_codec.cpp`'s own `decodeSegment()`/`decodeSegmentV4()` comments.
constexpr size_t kSceneSegmentBytesV3 = kSceneSegmentBytes + 1 + 2 + 2;
/// Schema 3 AND schema 4's own maximum header size (identical layout for
/// both -- only the per-segment flags/reversed byte differs, see
/// `kSceneSegmentBytesV3`'s own doc), reached only when every one of
/// `kMaxOutputs` slots is configured: magic(4)+schema(2)+size(2)+
/// generation(8)+output_count(1)+kMaxOutputs*(pin(2)+count(2)+order(1))+
/// map(width(2)+height(2)+first(2)+wiring(1))+scene.count(1)+brightness(1)+
/// paused(1)+reserved(2)+fx_catalog(4). A single-output topology's own
/// record is smaller (the header only ever carries `topology.count`
/// tuples, never a fixed kMaxOutputs of them) -- see encodeScene()'s own
/// comment.
constexpr size_t kSceneHeaderBytesV3Max =
    4 + 2 + 2 + 8 + 1 + kMaxOutputs * 5 + (2 + 2 + 2 + 1) + 1 + 1 + 1 + 2 + 4;
/// Schema 2's own maximum header size (this application's prior delivery,
/// no map/2D concept): magic(4)+schema(2)+size(2)+generation(8)+
/// output_count(1)+kMaxOutputs*5+scene.count(1)+brightness(1)+paused(1)+
/// reserved(2)+fx_catalog(4). Read-only from here on -- encodeScene() never
/// produces this anymore; decodeScene() still accepts it unchanged, for
/// every current-scene/preset record already saved before this delivery.
constexpr size_t kSceneHeaderBytesV2Max = 4 + 2 + 2 + 8 + 1 + kMaxOutputs * 5 + 1 + 1 + 1 + 2 + 4;
/// Schema 1's own fixed header size (published bench, before either
/// delivery): magic(4)+schema(2)+size(2)+generation(8)+pin(2)+count(2)+
/// order(1)+rgbw(1)+scene.count(1)+brightness(1)+paused(1)+reserved(3)+
/// fx_catalog(4). Read-only from here on.
constexpr size_t kSceneHeaderBytesV1 = 32;
/// Maximum total record size across all four schemas, 32 segments: schema
/// 3/4's own shared worst case (two configured outputs, a real map, every
/// segment 2D) always dominates schemas 1/2's own smaller maximums --
/// unchanged by schema 4's own flags-byte reinterpretation (Task 7G: same
/// byte count as schema 3, see `kSceneSegmentBytesV3`'s own doc).
constexpr size_t kSceneRecordBytes = kSceneHeaderBytesV3Max +
    kSceneSegmentBytesV3 * esp_pixels_composer::MAX_SEGMENTS + 4;

struct SceneRecord {
    uint8_t bytes[kSceneRecordBytes]{};
    size_t size = 0;
};

/// Encodes the complete scene against `topology`/`map` (schema 4, this
/// delivery's current format -- Task 7G) using explicit little-endian
/// fields. The record embeds both `topology` (every configured slot's
/// pin/count/order) and `map` (width/height/first/wiring, or the identity
/// sentinel) so a later reboot/recall can check compatibility before
/// applying the scene -- see `isCompatiblePrefix()`/`isCompatibleMap()`.
/// `record.size` varies with `scene.count`, `topology.count`, and whether
/// any segment is 2D (schema 4's own per-segment size is fixed regardless
/// of shape or `mirror` -- see `kSceneSegmentBytesV3` -- so only
/// `scene.count`/`topology.count` affect size, the same discipline this
/// codec already had before this delivery).
/// @param scene Source scene (1D and 2D segments may be freely mixed; every
///        segment validated against `map`'s own canvas dimensions when
///        `map` is a real matrix, or against `topology.totalPixels()` under
///        the identity bypass).
/// @param topology The scene's own active topology at save time -- must
///        satisfy `topology.valid()`.
/// @param map The scene's own active canvas/ledmap at save time -- must
///        satisfy `map.valid(topology.totalPixels())`.
/// @param sequence Monotonic journal generation; zero is reserved.
/// @param record Receives versioned, CRC-protected bytes, never raw C++ objects.
/// @return ESP_OK or validation error; no NVS access.
esp_err_t encodeScene(const esp_pixels_composer::AtScene &scene, const Topology &topology, const MapConfig &map,
                      uint64_t sequence, SceneRecord &record);

/// Validates all bytes/fields before changing either output -- including a
/// schema-1 or schema-2 record (both this application's own earlier,
/// map-less formats, read unchanged: their canvas is always treated as the
/// identity bypass), a schema-3 one (this application's own prior delivery,
/// map/2D-aware, its own per-segment byte still a strict 0/1 `reversed`
/// value, `mirror` always false), or a schema-4 one (this delivery, Task
/// 7G: that same byte position is instead a flags byte, bit0 reversed, bit1
/// mirror, any other bit rejected outright) -- then checks the record's own
/// embedded topology against `current_topology` via `isCompatiblePrefix()`,
/// AND its own embedded map against `current_map` via `isCompatibleMap()`.
/// Every segment is validated against the record's own canvas (never
/// `current_topology`'s/`current_map`'s), so a current device with more
/// outputs or a different (but still compatible) map can never make a
/// malformed old record valid.
/// @param record Complete record including CRC.
/// @param current_topology The device's own active topology right now; must
///        satisfy `current_topology.valid()`.
/// @param current_map The device's own active canvas/ledmap right now.
/// @param scene Receives the decoded scene on success only.
/// @param sequence Receives generation on success only.
/// @return ESP_OK. ESP_ERR_INVALID_SIZE/_CRC/_VERSION for a corrupt or
///         unrecognized (not schema 1, 2, 3, or 4) record. ESP_ERR_INVALID_ARG
///         for a structurally invalid record (including a schema-4 record
///         with an unknown flags bit set). ESP_ERR_NOT_SUPPORTED when the
///         record's own topology is not a compatible prefix of
///         `current_topology`, or its own map is not compatible with
///         `current_map` (see `isCompatibleMap()`) -- either way,
///         `scene`/`sequence` are left unchanged and the record itself is
///         never modified; the caller decides what "actionable" means for
///         its own context.
esp_err_t decodeScene(const SceneRecord &record, const Topology &current_topology, const MapConfig &current_map,
                      esp_pixels_composer::AtScene &scene, uint64_t &sequence);

/// Validates a stored scene against its own embedded topology/map, without
/// device compatibility filtering.
/// @param record Complete stored bytes.
/// @param scene Receives the structurally valid scene on success only; not permission to apply it.
/// @param sequence Receives its generation on success only.
/// @return ESP_OK or a validation error. Use decodeScene before applying to a device.
esp_err_t decodeStoredScene(const SceneRecord &record, esp_pixels_composer::AtScene &scene,
                            uint64_t &sequence);

} // namespace bench
