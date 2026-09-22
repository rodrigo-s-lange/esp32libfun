# EasyLED / pixels roadmap

Updated through the 2026-09-21 checkpoint. This roadmap belongs to the product
application and external pixels libraries; it does not expand the HAL core.

## Validated baseline

- Composer `c2c3e542ce00b60c9ae4c7d224244676bb8a2de1`: two physical
  adapter outputs, managed rendering, 1D/2D segments, AT controls and
  confirmed-scene export/load and symmetric 1D mirror.
- FX `4e977137950a30c81f4151b411b1541fba85adb4`: XYGradient added and
  validated on the bench; old effect IDs retained.
- Application: this local Mirror checkpoint, extending the 2D checkpoint
  `d6e5665`, multiple outputs and named presets. All remain unreleased.
- WROOM-32 / COM6: GPIO16 with 50 and GPIO17 with 12 WS2812 RGB pixels,
  both GRB, 115200 baud. Output configuration survives reboot.
- 6760 executed checks passed (6588 scene/AT/preset/output/map/XY, 172 adapter).
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
| 5 — next | Custom effect parameters via AT | Discover labels/usage flags, edit the three existing custom fields, publish and retain them without disturbing unrelated settings. |
| 6 | Expand the effect catalog | Deliver small batches of useful effects with appropriate controls, examples and physical checks; include 2D effects as the panel capability develops. |

The Composer engine already supports 2D/ledmaps/multiple outputs in C++.
The reviewed `adapter_esp_pixels` supports two physical RGB outputs. The
application's schema 4 retains mirror, topology, the map descriptor and 1D/2D
shape while reading schema-1/2/3 records. Record sizes remain 1295/1348 bytes
for scenes/presets. Hardware suites and storage
capacity passed; Rodrigo confirmed the 2D increment visually and it is
checkpointed locally. The bench used a virtual canvas on a straight strip;
an assembled matrix panel has not been tested.

## Working cycle

One cohesive task -> build -> execute -> fix concrete failures -> local
checkpoint. Reviews focus on behavior and material regressions; secondary
improvements are recorded without creating repeated documentation-only gates.

Claude implements cohesive briefs; Codex reviews, integrates and runs the
bench. Distinguish compilation, automated execution, serial confirmation and
operator visual confirmation. Announce and obtain confirmation before visual
changes; arrange extra output wiring with Rodrigo before touching hardware.
Do not infer push/tag/flash authorization from an earlier delivery.

Next task: [custom effect parameters via AT](pixels-custom-params-task.md),
including one Stripes consumer so the three controls are usable on the bench.
Completed briefs: [symmetric mirror](pixels-mirror-task.md),
[2D canvas / ledmap](pixels-2d-task.md),
[multiple physical outputs](pixels-outputs-task.md) and
[named presets](pixels-presets-task.md).
