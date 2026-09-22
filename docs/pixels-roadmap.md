# EasyLED / pixels roadmap

Updated through the 2026-09-22 delivery alignment. This roadmap belongs to the product
application and external pixels libraries; it does not expand the HAL core.

## Validated baseline

- Composer `d575dbe06ff713a954c13fbac60f58ba43613f4d`: two physical
  adapter outputs, managed rendering, 1D/2D segments, AT controls and
  confirmed-scene export/load, symmetric 1D mirror and three custom AT controls.
- FX `6a93967c9499f4e72e24b2e21cdad4b3b4428926`: XYGradient and Stripes
  validated on the bench; old effect IDs retained.
- Application: this local custom-parameter checkpoint, extending Mirror and the 2D checkpoint
  `d6e5665`, multiple outputs and named presets. All remain unreleased.
- WROOM-32 / COM6: GPIO16 with 50 and GPIO17 with 12 WS2812 RGB pixels,
  both GRB, 115200 baud. Output configuration survives reboot.
- 7260 executed checks passed (7088 scene/AT/preset/output/map/XY/Stripes, 172 adapter).
  The prior real-NVS run passed 201 maximum-size 1348-byte writes, full-store replacement,
  rejection of a 101st name, and 100 recalls after reboot. The original
  preset partition was restored and verified against its backup.
- Rodrigo confirmed virtual 5x10 row/column routing, X/Y/mixed gradients,
  progressive/serpentine mapping and blackout/preset/reboot recovery on the
  straight 50-pixel strip (2026-09-16, reaffirmed 2026-09-21). The earlier
  two-output boundary demonstration remains validated. All 100 original
  presets then passed serial geometry/color checks. Last bench state:
  IDENTITY, Pos100 (LED 34 red, second output black), brightness 96.
- Rodrigo also confirmed even50/odd49 Mirror, reverse interaction, Sinelon
  crossing both outputs and blackout/preset/reboot recovery. Legacy recall
  exposed an AT-task stack overflow; redundant whole-scene temporaries were
  removed from the application preset handlers, retaining the core stack size.
- A single green flash during startup persists. Cause unconfirmed; Rodrigo
  accepted continuing with it as a known non-blocking issue on 2026-09-11.
- Rodrigo confirmed the custom-parameter/Stripes sequence after a slower replay:
  independent controls, Mirror/reverse, green bands crossing both outputs and
  blackout/preset/reboot recovery. Original partitions were restored and
  verified; he confirmed final LED34 red. Suite fixture corrections required
  no production change. Scene-runner static DRAM headroom: 2208 B.

## Delivery order

The order below is the proposed implementation sequence. Named presets, 2D
and multiple outputs are explicit MVP requirements. Symmetric mirror,
custom parameters and new effects are also in the roadmap; their placement
here does not establish an agreed exclusion from the MVP.

| Order | Delivery | Acceptance result |
|---|---|---|
| 1 — local checkpoint | Named presets via AT, 100 positions | Delivered: save/list/load/delete, full-size capacity and replacement, reboot recovery. Startup flash retained as an accepted known issue. |
| 2 — local checkpoint | Multiple physical outputs | Delivered: 5652 automated hardware checks, GPIO16/50 and GPIO17/12 retained, boundary crossing and preset/reboot recovery visually confirmed. Maximum-size NVS capacity/replacement passed; original 100 presets restored and recalled. |
| 3 — local checkpoint | 2D / ledmap through AT and persistence | Delivered: 6352 automated hardware checks, real-NVS maximum-size capacity/replacement and visual confirmation of virtual 5x10 coordinates, XYGradient, progressive/serpentine mapping and preset/reboot recovery. Original 100 presets restored and verified. |
| 4 — local checkpoint | Symmetric mirror per segment | Delivered: 6760 hardware checks, even/odd symmetry, reverse interaction, cross-output animation and preset/reboot recovery visually confirmed. Legacy recall stack overflow corrected in the application. |
| 5 — local checkpoint | Custom effect parameters via AT + Stripes | Delivered: 7260 automated checks, serial and visual custom controls, Mirror/reverse, crossing and preset/reboot recovery. All 100 originals restored and verified; final LED34 red confirmed. |
| 6 | Expand the effect catalog | Deliver small batches of useful effects with appropriate controls, examples and physical checks; include 2D effects as the panel capability develops. |

The Composer engine already supports 2D/ledmaps/multiple outputs in C++.
The reviewed `adapter_esp_pixels` supports two physical RGB outputs. The
application's schema 4 retains mirror, topology, the map descriptor and 1D/2D
shape while reading schema-1/2/3 records. Record sizes remain 1295/1348 bytes
for scenes/presets. Hardware suites and storage
capacity passed; Rodrigo confirmed the 2D increment visually and it is
checkpointed locally. The bench used a virtual canvas on a straight strip;
an assembled matrix panel has not been tested.

## Delivery alignment: 2026-10-15

Rodrigo approved the revised sequencing on 2026-09-22 without removing any
agreed scope. The interface is being developed in the separate EasyLED project.
This repository and the external pixels libraries own firmware capabilities,
hardware validation and the integration contracts needed by that interface.
Do not start a duplicate interface here. Context review on 2026-09-22 checked
EasyLED's remote main `306fa44` (the local Windows checkout was older):
[control contract](https://github.com/rodrigo-s-lange/easyled-platform/blob/main/docs/contracts/easyled-control-contract.md),
[segment model](https://github.com/rodrigo-s-lange/easyled-platform/blob/main/api/go-api/models/segment.go)
and [product firmware issue #48](https://github.com/rodrigo-s-lange/easyled-platform/issues/48).
The agreed transport is MQTT commands/state/events with HTTPS payload delivery
and local rendering, including offline operation. Product-specific network
orchestration belongs to EasyLED firmware, consuming the libraries directly.

The next integration increment must align backend payloads with the capabilities
already delivered here: mirror/reverse, three custom bytes, color-source
selection and the effects appended after IDs 0..2. EasyLED's current segment
model does not yet carry those controls. Preserve effect IDs and metadata;
add catalog updates through a new migration in that repository. Also make the
mapping between backend preset UUID/version and the device's 100 local
positions explicit. Its full embedded ledmap and capability manifest must
use the real Composer/output limits, not assume the bench's rectangular map
descriptor is the complete product contract. Backend delivery/bootstrap and
command pipelines remain tracked in EasyLED's existing issues; this review
does not claim those integrations have already run on the device.

Custom parameters/Stripes have passed review, hardware execution and operator
visual confirmation, with local checkpoints. Subsequent effect
batches and EasyLED integration can progress alongside each other once their
shared contracts and file ownership are clear. Prioritize an operable flow:
configure outputs, select an effect, adjust controls, save/recall a preset and
recover after reboot. Preserve presets, 2D, multiple outputs, Mirror, custom
parameters and new effects in the agreed scope.

Planning targets (coordinate with EasyLED; not claims of completed work):

- By 2026-09-25: finish the current increment and align the delivery's user flow.
- By 2026-10-02: first integrated flow controlling real LEDs.
- By 2026-10-09: integrate the MVP capabilities and prioritized effect batches.
- 2026-10-10 through 14: fixes, installation/recovery checks and delivery preparation.
- 2026-10-15: delivery of the validated version.

## Working cycle

One cohesive task -> build -> execute -> fix concrete failures -> local
checkpoint. Reviews focus on behavior and material regressions; secondary
improvements are recorded without creating repeated documentation-only gates.

Claude implements cohesive briefs; Codex reviews, integrates and runs the
bench. Distinguish compilation, automated execution, serial confirmation and
operator visual confirmation. Announce and obtain confirmation before visual
changes; arrange extra output wiring with Rodrigo before touching hardware.
Do not infer push/tag/flash authorization from an earlier delivery.

Next increment: a focused effect batch (Blink, Breathe and Chase), while the
EasyLED team aligns product payloads and catalog entries with firmware capabilities.
Completed briefs: [custom effect parameters via AT](pixels-custom-params-task.md),
[symmetric mirror](pixels-mirror-task.md),
[2D canvas / ledmap](pixels-2d-task.md),
[multiple physical outputs](pixels-outputs-task.md) and
[named presets](pixels-presets-task.md).
