#pragma once

#include "esp_pixels_composer_at_scene.hpp"
#include <cstddef>
#include <cstdint>

namespace bench {

constexpr uint16_t kPixelPin = 16;
constexpr uint16_t kPixelCount = 50;
constexpr size_t kSceneHeaderBytes = 32;
constexpr size_t kSceneSegmentBytes = 34;
constexpr size_t kSceneRecordBytes = kSceneHeaderBytes +
    kSceneSegmentBytes * esp_pixels_composer::MAX_SEGMENTS + 4;

struct SceneRecord {
    uint8_t bytes[kSceneRecordBytes]{};
    size_t size = 0;
};

/// Encodes the complete bench scene using explicit little-endian fields.
/// @param scene Source scene (1D, fixed bench topology).
/// @param sequence Monotonic journal generation; zero is reserved.
/// @param record Receives versioned, CRC-protected bytes, never raw C++ objects.
/// @return ESP_OK or validation error; no NVS access.
esp_err_t encodeScene(const esp_pixels_composer::AtScene &scene, uint64_t sequence,
                      SceneRecord &record);

/// Validates all bytes/fields before changing either output.
/// @param record Complete record including CRC.
/// @param scene Receives decoded scene on success only.
/// @param sequence Receives generation on success only.
/// @return ESP_OK or invalid/incompatible data error; no NVS access.
esp_err_t decodeScene(const SceneRecord &record, esp_pixels_composer::AtScene &scene,
                      uint64_t &sequence);

} // namespace bench
