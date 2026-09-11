# EasyLED / pixels roadmap

Updated with Rodrigo on 2026-09-11. This roadmap belongs to the product
application and external pixels libraries; it does not expand the HAL core.

## Validated baseline

- Composer `0730626d2842a55d980b0848e636b579ecc81c62`: managed rendering,
  segments, effects, AT controls and confirmed-scene export/load.
- Application checkpoint `08966f65f297bf62b73a10d04f1b0b1ff01087f6`:
  100 named presets, real-NVS capacity/replacement and reboot recovery.
- WROOM-32 / COM6 / GPIO16 / 50 WS2812 RGB pixels, GRB, 115200 baud.
- 5224 executed checks passed. Rodrigo confirmed red/blue preset recall
  after reboot; all 100 RGB-per-pixel presets were saved and recalled with
  serial geometry/color checks. Device remains on Pos100 (LED 34 red).
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
| 2 — next | Multiple physical outputs | Configure and drive at least two outputs through the product API, persist their configuration, and validate with separately connected strips. |
| 3 | 2D / ledmap through AT and persistence | Configure a mapped panel, recall its scene after reboot, and demonstrate a spatial pattern/effect that proves the mapping. Add the minimum 2D effect needed for this demonstration. |
| 4 | Symmetric mirror per segment | Demonstrate symmetric duplication, distinct from reverse, with documented odd/even-length behavior, AT control and persistence. |
| 5 | Custom effect parameters via AT | Discover labels/usage flags, edit the three existing custom fields, publish and retain them without disturbing unrelated settings. |
| 6 | Expand the effect catalog | Deliver small batches of useful effects with appropriate controls, examples and physical checks; include 2D effects as the panel capability develops. |

The Composer engine already supports 2D/ledmaps/multiple outputs in C++.
The concrete `adapter_esp_pixels` currently exposes one RGB output: the
multiple-output delivery must include the adapter work, not just AT commands.
The current application schema is fixed to one 1D bench output. Topology
deliveries must evolve persistence deliberately, including preset compatibility
and preservation of existing valid saved data.

## Working cycle

One cohesive task -> build -> execute -> fix concrete failures -> local
checkpoint. Reviews focus on behavior and material regressions; secondary
improvements are recorded without creating repeated documentation-only gates.

Claude implements cohesive briefs; Codex reviews, integrates and runs the
bench. Distinguish compilation, automated execution, serial confirmation and
operator visual confirmation. Announce and obtain confirmation before visual
changes; arrange extra output wiring with Rodrigo before touching hardware.
Do not infer push/tag/flash authorization from an earlier delivery.

Next task: [multiple physical outputs](pixels-outputs-task.md).
Previous brief: [named presets](pixels-presets-task.md).
