#pragma once

#include <cstddef>
#include <cstdint>

// The device's own 2D canvas/ledmap configuration -- a rectangular matrix
// descriptor (width/height/first logical pixel/wiring), generated into
// a real `Composer::setLedmap()` table at boot. Entirely application/device
// configuration, deliberately separate from effect segments (which only
// ever address the resulting canvas/logical index space) and from the
// transport-neutral Composer/Composer sidecar -- mirrors output_config.hpp's
// own split exactly (map_store.hpp/map_at.hpp own the NVS/AT concepts this
// header does not). No adapter/Composer include here on purpose, so
// scene_codec.hpp (which needs only the plain data below) stays as light as
// it already was.
//
// This first product increment is deliberately a single, uniform matrix for
// the WHOLE canvas -- no arbitrary map-entry editor, no holes, no panel
// tiling/rotation, no separate canvas per output. The Composer's own real
// ledmap (`setLedmap()`) supports all of that already; these are limits of
// this first AT-level increment, not the engine underneath it.

namespace bench {

/// Wiring for a generated matrix -- stable stored codes, independent of any
/// transport enum. Origin remains top-left for every mode.
enum class MapWiring : uint8_t {
    Progressive = 0, ///< every row runs the same direction (left -> right)
    Serpentine = 1,  ///< row 0 forward, row 1 backward, alternating
    ColumnSerpentine = 2, ///< column 0 bottom -> top, column 1 top -> bottom, alternating
};

/// The device's active, staged, or saved 2D canvas configuration. `width ==
/// 0` is the distinguished **identity** value (the default): no real matrix
/// installed, `Composer`'s own identity-bypass canvas applies
/// (`canvasWidth() == totalPixels()`, `canvasHeight() == 1`) -- a real
/// matrix can never itself have `width == 0` (see `valid()`), so this
/// sentinel never collides with a genuine configuration.
struct MapConfig {
    uint16_t width = 0;  ///< 0 => identity; otherwise matrix width, > 0
    uint16_t height = 1; ///< meaningful only when width != 0; > 0
    uint16_t first = 0;  ///< global logical index of canvas cell (0,0); meaningful only when width != 0
    MapWiring wiring = MapWiring::Progressive; ///< meaningful only when width != 0

    /// @return true iff this is the distinguished identity value
    ///         (`width == 0`) -- no real matrix, `Composer`'s own
    ///         identity-bypass canvas applies.
    bool isIdentity() const { return width == 0; }

    /// @return true iff identity is canonical (0,1,0,Progressive), or every field is in range
    ///         AND the matrix fits within `total_pixels`: `width > 0`,
    ///         `height > 0`, `first + width*height <= total_pixels`.
    ///         `width`/`height`/`first` are all `uint16_t`, so
    ///         `width*height` (at most 65535*65535) and the further `+
    ///         first` both fit a `uint32_t` accumulator without overflow --
    ///         no checked-arithmetic helper is needed here.
    bool valid(uint16_t total_pixels) const
    {
        if (isIdentity()) {
            return height == 1 && first == 0 && wiring == MapWiring::Progressive;
        }
        if (height == 0 || static_cast<uint8_t>(wiring) > 2) {
            return false;
        }
        const uint32_t cells = static_cast<uint32_t>(width) * height;
        return static_cast<uint32_t>(first) + cells <= total_pixels;
    }
};

inline bool operator==(const MapConfig &a, const MapConfig &b)
{
    if (a.isIdentity() || b.isIdentity()) {
        return a.isIdentity() && b.isIdentity();
    }
    return a.width == b.width && a.height == b.height && a.first == b.first && a.wiring == b.wiring;
}
inline bool operator!=(const MapConfig &a, const MapConfig &b) { return !(a == b); }

/// Compatibility rule shared by the current-scene and preset codecs
/// (mirrors `isCompatiblePrefix()`'s own role for output topology, but maps
/// have no natural "prefix" notion -- a matrix depends on its exact
/// dimensions/origin/wiring, not just an appended tail):
///
/// - Identity records retain their flat mapping under IDENTITY. All-1D
///   identity records can also use the progressive-map rule below.
/// - A record whose own embedded map is a real matrix requires an EXACT
///   match (`==`) with the current map -- "new 2D records require...  the
///   same canvas/map semantics."
/// Identity-only 1D records may also use a zero-offset progressive matrix
/// covering their entire original flat domain. Pass zero for any 2D scene:
/// changing its canvas dimensions is not a compatible flat-index change.
/// @param record_map Stored layout.
/// @param current_map Active layout, already validated against its outputs.
/// @param identity_domain Original pixel count for an all-1D identity scene, otherwise zero.
/// @return true iff `record_map` may be recalled under `current_map`.
inline bool isCompatibleMap(const MapConfig &record_map, const MapConfig &current_map,
                            uint16_t identity_domain = 0)
{
    if (record_map.isIdentity()) {
        return current_map.isIdentity() ||
            (identity_domain != 0 && current_map.first == 0 &&
             current_map.wiring == MapWiring::Progressive &&
             static_cast<uint32_t>(current_map.width) * current_map.height >= identity_domain);
    }
    return record_map == current_map;
}

/// Generates a real `width*height` row-major ledmap table (flat canvas
/// index -> logical index, `Composer::setLedmap()`'s own format) for a
/// rectangular matrix. PROGRESSIVE assigns every row left-to-right in
/// increasing logical order. SERPENTINE alternates direction per row (row 0
/// forward, row 1 backward, ...). COLUMN_SERPENTINE stores each physical
/// column contiguously: column 0 runs bottom-to-top, column 1 top-to-bottom,
/// alternating thereafter, while canvas coordinate `(0,0)` remains the
/// top-left cell. Never produces a hole (`-1`) itself -- every cell
/// of the full `width*height` rectangle maps to exactly one logical index
/// in `[first, first+width*height)`, each used exactly once.
/// @param map A non-identity, `valid()` map (caller's own responsibility;
///        not re-validated here).
/// @param entries Caller-owned buffer, at least `map.width*map.height` cells.
/// @param entry_count Capacity of `entries`.
/// @return true on success; false if `map.isIdentity()`, `entries` is
///         null, or `entry_count` is too small.
inline bool generateLedmap(const MapConfig &map, int32_t *entries, size_t entry_count)
{
    if (map.isIdentity() || entries == nullptr) {
        return false;
    }
    const size_t cells = static_cast<size_t>(map.width) * map.height;
    if (entry_count < cells) {
        return false;
    }
    for (uint16_t y = 0; y < map.height; ++y) {
        for (uint16_t x = 0; x < map.width; ++x) {
            int32_t logical = 0;
            if (map.wiring == MapWiring::ColumnSerpentine) {
                const uint16_t row = (x % 2) == 0 ? static_cast<uint16_t>(map.height - 1 - y) : y;
                logical = static_cast<int32_t>(x) * map.height + row;
            } else {
                const bool reverse_row = (map.wiring == MapWiring::Serpentine) && ((y % 2) != 0);
                const uint16_t col = reverse_row ? static_cast<uint16_t>(map.width - 1 - x) : x;
                logical = static_cast<int32_t>(y) * map.width + col;
            }
            entries[static_cast<size_t>(y) * map.width + x] = static_cast<int32_t>(map.first) + logical;
        }
    }
    return true;
}

} // namespace bench
