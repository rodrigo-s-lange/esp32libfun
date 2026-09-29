# EasyLED / pixels roadmap

Updated through the 2026-09-28 firmware checkpoint. This roadmap belongs to the product
application and external pixels libraries; it does not expand the HAL core.

## Validated baseline

- Composer `17b5e9a6ef6e9701b1336bfd9cc03470173c8449`: two physical
  adapter outputs, managed rendering, 1D/2D segments, AT controls and
  confirmed-scene export/load, symmetric 1D mirror and three custom AT controls.
- FX `9de81da4848df09be3b2c5aa4156a1e160ceb84c`: effects 0..8,
  including XYGradient, Stripes, Blink, Breathe and Chase; old IDs retained.
- Application branch `checkpoint/wroom-composer-app`: named presets, multiple
  outputs, 2D, Mirror, custom controls, effects 0..8, the offline EasyLED
  firmware-contract boundary, command executor and durable command storage.
  All remain unreleased.
- WROOM-32 / COM6: GPIO16 with 50 WS2812 RGB pixels, GRB, 115200 baud.
  GPIO17/12 is currently physically inactive. Output configuration survives reboot.
- 7662 executed hardware checks passed (7490 combined scene/AT/persistence/
  effects and 172 adapter), both with zero failures.
  The prior real-NVS run passed 201 maximum-size 1348-byte writes, full-store replacement,
  rejection of a 101st name, and 100 recalls after reboot. The original
  preset partition was restored and verified against its backup at that checkpoint.
- Rodrigo confirmed virtual 5x10 row/column routing, X/Y/mixed gradients,
  progressive/serpentine mapping and blackout/preset/reboot recovery on the
  straight 50-pixel strip (2026-09-16, reaffirmed 2026-09-21). The earlier
  two-output boundary demonstration remains validated. All 100 original
  presets then passed serial geometry/color checks. Last bench state:
  IDENTITY, no active scene, GPIO16/50, disposable preset cache 0/100.
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
- Blink/Breathe/Chase passed 7490 combined checks in QEMU and on WROOM.
  Rodrigo confirmed the hard blink, smooth breathe, Chase direction at slow/
  fast rates and persisted blackout after reboot on GPIO16/50 on 28/09/26.
- The offline EasyLED path parses the four closed command types, verifies
  exact-byte SHA-256, validates config/preset payloads, drives a real Composer
  candidate and enforces terminal event/rollback ordering. Its NVS-backed
  eight-entry terminal journal and 8192-byte A/B staging recover across reboot.
  The focused QEMU target passed 259 checks. The combined network application
  builds with the component, but `main.cpp` does not yet route live MQTT/HTTPS
  traffic into the executor.

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
| 6 — local checkpoint | Expand the effect catalog: Blink, Breathe, Chase | Delivered: ids 6..8, deterministic timing fixes, 7490 hardware checks and visual confirmation on GPIO16/50. |
| 7 — local checkpoint | Offline EasyLED firmware-contract boundary | Delivered: bounded envelope parser, SHA-256, events and durable interfaces; 52 QEMU checks. |
| 8 - local checkpoint | Offline EasyLED command executor | Delivered: provisional payload adapter, ordered execution, duplicate replay, rollback and real Composer activation; 153 QEMU checks before the storage increment. |
| 9 - local checkpoint | Durable EasyLED command storage | Delivered: dedicated NVS partition, immutable eight-entry terminal journal, 8192-byte A/B staging, terminal-aware reboot recovery and corruption rejection; full focused target now passes 259 QEMU checks. |
| 10 - next | Wire the local control task | Instantiate executor/storage in the combined app, keep work outside MQTT callbacks, add concrete payload/event/time/active-target adapters and publish state. |

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

The next integration increment must wire the already validated executor and
storage into the combined application's control task while EasyLED aligns its
payloads with the capabilities delivered here: mirror/reverse, three custom
bytes, color-source selection and effects appended after IDs 0..2. Preserve
effect IDs and metadata. The 100 local positions are a disposable device cache;
the product contract still needs an explicit mapping from backend preset UUID
and checksum to a cache position. The full embedded ledmap and capability
manifest must use the real Composer/output limits. Backend delivery/bootstrap
and command pipelines remain tracked in EasyLED's existing issues; this review
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

Next increment: wire the validated executor and durable storage into the
combined application's control task while the EasyLED team closes the remaining
canonical payload and delivery contracts. Completed briefs:
[offline EasyLED command executor](easyled-command-executor-task.md),
[Blink, Breathe and Chase](pixels-effects-batch-task.md),
[custom effect parameters via AT](pixels-custom-params-task.md),
[symmetric mirror](pixels-mirror-task.md),
[2D canvas / ledmap](pixels-2d-task.md),
[multiple physical outputs](pixels-outputs-task.md) and
[named presets](pixels-presets-task.md).
