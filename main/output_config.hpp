#pragma once

#include <cstdint>
#include "esp_bit_defs.h"
#include "soc/soc_caps.h"

// The device's own physical output topology -- pins, per-output pixel
// counts and wire color order. Entirely application/device configuration,
// deliberately separate from effect segments (which only ever address the
// resulting GLOBAL logical index space, Design v2 §7 space 4) and from the
// transport-neutral Composer/Composer sidecar. This header has no NVS/AT
// concept of its own -- see output_store.hpp/outputs_at.hpp for those. No
// adapter/Composer include here on purpose, so scene_codec.hpp (which needs
// only the plain data below) stays as light as it already was; main.cpp
// static_asserts this against EspPixelsSink::kMaxOutputs so the two can
// never silently drift apart.

namespace bench {

/// This application's own supported output-slot ceiling for this cut, kept
/// equal to `EspPixelsSink::kMaxOutputs` (the adapter's own delivered
/// limit; see its class doc for why this is neither `esp_pixels_composer::
/// MAX_OUTPUTS`, a software table size, nor a hardware promise) -- verified
/// by a `static_assert` in main.cpp, the one file that already includes
/// both headers.
constexpr unsigned kMaxOutputs = 2;
constexpr uint16_t kMaxPixelsPerOutput = 2048; ///< Application startup/frame-buffer limit.
constexpr uint8_t kOutputOrderCount = 6; ///< GRB, RGB, RBG, GBR, BRG, BGR (stable stored codes).

/// Checks output capability and protects this WROOM application's flash/UART pins.
/// @param pin Candidate GPIO number.
/// @return true for an output-capable pin not reserved by flash or the AT console.
inline bool validOutputPin(int pin)
{
    return pin >= 0 && pin < SOC_GPIO_PIN_COUNT &&
        ((SOC_GPIO_VALID_OUTPUT_GPIO_MASK >> pin) & 1ULL) != 0 &&
        !(pin >= 6 && pin <= 11) && pin != 1 && pin != 3;
}

/// This application's fixed default, unchanged since before this delivery:
/// one GPIO16/50/GRB output. Boot uses this whenever no valid saved output
/// configuration exists, or a saved one is rejected -- see main.cpp.
constexpr int16_t kDefaultPin = 16;
constexpr uint16_t kDefaultCount = 50;
constexpr uint8_t kDefaultOrder = 0; ///< Stable stored code for GRB.

/// One physical output slot. Stored order codes: 0=GRB, 1=RGB, 2=RBG,
/// 3=GBR, 4=BRG, 5=BGR; independent from the transport enum's values.
struct OutputSpec {
    int16_t pin = -1;  ///< -1: this slot is absent/unconfigured
    uint16_t count = 0;
    uint8_t order = 0; ///< Stable RGB byte-order code, 0..5.
};

/// The device's active or saved output topology: 1..kMaxOutputs configured
/// slots, in order. Segments retain GLOBAL logical indices across every
/// slot -- for counts 50 and 20, slot 0 is canvas [0,50), slot 1 is
/// [50,70); AT+SEGSET never uses a second, per-output numbering scheme.
struct Topology {
    OutputSpec outputs[kMaxOutputs];
    uint8_t count = 1; ///< number of configured slots, 1..kMaxOutputs

    /// @return Sum of every configured slot's own pixel count -- the total
    ///         size of the global logical/canvas index space this topology
    ///         establishes.
    uint16_t totalPixels() const
    {
        uint16_t total = 0;
        for (uint8_t i = 0; i < count; ++i) {
            total += outputs[i].count;
        }
        return total;
    }

    /// @return true iff every field is in range: `1 <= count <=
    ///         kMaxOutputs`, every pin passes validOutputPin(), each count
    ///         is 1..kMaxPixelsPerOutput, order is 0..5, pins differ, and
    ///         `totalPixels()` does not overflow `uint16_t`.
    bool valid() const
    {
        if (count == 0 || count > kMaxOutputs) {
            return false;
        }
        uint32_t total = 0;
        for (uint8_t i = 0; i < count; ++i) {
            const OutputSpec &o = outputs[i];
            if (!validOutputPin(o.pin) || o.count == 0 ||
                o.count > kMaxPixelsPerOutput || o.order >= kOutputOrderCount) {
                return false;
            }
            for (uint8_t j = 0; j < i; ++j) {
                if (outputs[j].pin == o.pin) {
                    return false;
                }
            }
            total += o.count;
        }
        return total <= 0xFFFFU;
    }
};

/// @return The fixed single-output default: `{pin=kDefaultPin,
///         count=kDefaultCount, order=kDefaultOrder}`, `count=1`.
inline Topology defaultTopology()
{
    Topology t;
    t.count = 1;
    t.outputs[0] = OutputSpec{kDefaultPin, kDefaultCount, kDefaultOrder};
    return t;
}

/// Prefix-compatibility rule shared by the current-scene and preset codecs:
/// a record saved against `record_topology` may be recalled under
/// `current_topology` iff EVERY slot `record_topology` knows about matches
/// `current_topology`'s own same-index slot exactly (pin, count, order) --
/// `current_topology` may have MORE slots than the record does (an old
/// single-output record recalled after a second output was appended: the
/// original slot 0 keeps its meaning, the added slot is simply not covered
/// by any of that record's own segments). `current_topology` may never have
/// FEWER slots than the record expects.
/// @return true iff `record_topology` is a compatible prefix of `current_topology`.
inline bool isCompatiblePrefix(const Topology &record_topology, const Topology &current_topology)
{
    if (!record_topology.valid() || !current_topology.valid() ||
        record_topology.count > current_topology.count) {
        return false;
    }
    for (uint8_t i = 0; i < record_topology.count; ++i) {
        const OutputSpec &a = record_topology.outputs[i];
        const OutputSpec &b = current_topology.outputs[i];
        if (a.pin != b.pin || a.count != b.count || a.order != b.order) {
            return false;
        }
    }
    return true;
}

} // namespace bench
