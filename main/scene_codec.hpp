#pragma once

#include "esp_pixels_composer_at_scene.hpp"
#include "output_config.hpp"
#include <cstddef>
#include <cstdint>

namespace bench {

constexpr size_t kSceneSegmentBytes = 34;
/// Schema 2's own maximum header size, reached only when every one of
/// `kMaxOutputs` slots is configured: magic(4)+schema(2)+size(2)+
/// generation(8)+output_count(1)+kMaxOutputs*(pin(2)+count(2)+order(1))+
/// scene.count(1)+brightness(1)+paused(1)+reserved(2)+fx_catalog(4). A
/// single-output topology's own record is smaller (the header only ever
/// carries `topology.count` tuples, never a fixed kMaxOutputs of them) --
/// see encodeScene()'s own comment.
constexpr size_t kSceneHeaderBytesV2Max = 4 + 2 + 2 + 8 + 1 + kMaxOutputs * 5 + 1 + 1 + 1 + 2 + 4;
/// Schema 1's own fixed header size (published bench, before this
/// delivery): magic(4)+schema(2)+size(2)+generation(8)+pin(2)+count(2)+
/// order(1)+rgbw(1)+scene.count(1)+brightness(1)+paused(1)+reserved(3)+
/// fx_catalog(4). Read-only from here on -- encodeScene() never produces
/// this anymore; decodeScene() still accepts it unchanged, for every
/// current-scene/preset record already saved before this cut.
constexpr size_t kSceneHeaderBytesV1 = 32;
/// Maximum total record size across both schemas, 32 segments: schema 2's
/// own worst case (two configured outputs) always dominates schema 1's.
constexpr size_t kSceneRecordBytes = kSceneHeaderBytesV2Max +
    kSceneSegmentBytes * esp_pixels_composer::MAX_SEGMENTS + 4;

struct SceneRecord {
    uint8_t bytes[kSceneRecordBytes]{};
    size_t size = 0;
};

/// Encodes the complete scene against `topology` (schema 2, this delivery's
/// current format) using explicit little-endian fields. The record embeds
/// `topology` itself (every configured slot's pin/count/order) so a later
/// reboot/recall can check compatibility before applying the scene --see
/// `isCompatiblePrefix()`. `record.size` varies with both `scene.count` and
/// `topology.count`, the same discipline this codec already had for segment
/// count alone.
/// @param scene Source scene (1D; every segment's `stop` validated against
///        `topology.totalPixels()`, never a fixed constant).
/// @param topology The scene's own active topology at save time -- must
///        satisfy `topology.valid()`.
/// @param sequence Monotonic journal generation; zero is reserved.
/// @param record Receives versioned, CRC-protected bytes, never raw C++ objects.
/// @return ESP_OK or validation error; no NVS access.
esp_err_t encodeScene(const esp_pixels_composer::AtScene &scene, const Topology &topology, uint64_t sequence,
                      SceneRecord &record);

/// Validates all bytes/fields before changing either output -- including a
/// schema-1 record (this application's own published-bench format, read
/// unchanged: its single embedded pin/count/order is treated as a
/// one-output topology) or a schema-2 one (this delivery's format, embedding
/// 1..kMaxOutputs configured slots) -- then checks the record's own embedded
/// topology against `current_topology` via `isCompatiblePrefix()`. Every
/// segment is validated against the record's own pixel count, so appended
/// outputs cannot make a malformed old record valid. Additional outputs
/// are uncovered by the old record's segments.
/// @param record Complete record including CRC.
/// @param current_topology The device's own active topology right now; must
///        satisfy `current_topology.valid()`.
/// @param scene Receives the decoded scene on success only.
/// @param sequence Receives generation on success only.
/// @return ESP_OK. ESP_ERR_INVALID_SIZE/_CRC/_VERSION for a corrupt or
///         unrecognized (neither schema 1 nor 2) record. ESP_ERR_INVALID_ARG
///         for a structurally invalid record. ESP_ERR_NOT_SUPPORTED when
///         topology is not a compatible prefix of `current_topology` --
///         either way, `scene`/`sequence` are left unchanged and the record
///         itself is never modified; the caller decides what "actionable"
///         means for its own context (e.g. naming the mismatched pin/count).
esp_err_t decodeScene(const SceneRecord &record, const Topology &current_topology,
                      esp_pixels_composer::AtScene &scene, uint64_t &sequence);

/// Validates a stored scene against its embedded topology, without device compatibility filtering.
/// @param record Complete stored bytes.
/// @param scene Receives the structurally valid scene on success only; not permission to apply it.
/// @param sequence Receives its generation on success only.
/// @return ESP_OK or a validation error. Use decodeScene before applying to a device.
esp_err_t decodeStoredScene(const SceneRecord &record, esp_pixels_composer::AtScene &scene,
                            uint64_t &sequence);

} // namespace bench
