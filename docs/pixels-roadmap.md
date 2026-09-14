# EasyLED / pixels roadmap

Updated through the 2026-09-14 bench. This roadmap belongs to the product
application and external pixels libraries; it does not expand the HAL core.

## Validated baseline

- Composer `07eb426a9c72f4b53062ba62d3a9a63370534755`: two physical
  adapter outputs, managed rendering, segments, effects, AT controls and
  confirmed-scene export/load.
- Application: this local multiple-output checkpoint, extending the named
  preset checkpoint `08966f65f297bf62b73a10d04f1b0b1ff01087f6`.
- WROOM-32 / COM6: GPIO16 with 50 and GPIO17 with 12 WS2812 RGB pixels,
  both GRB, 115200 baud. Output configuration survives reboot.
- 5652 executed checks passed (5480 scene/AT/preset/output, 172 adapter).
  Real NVS passed 201 maximum-size 1181-byte writes, full-store replacement,
  rejection of a 101st name, and 100 recalls after reboot. The original
  preset partition was restored and verified against its backup.
- Rodrigo confirmed red/blue outputs, green across their boundary, explicit
  blackout/preset recall and reboot recovery. All 100 original RGB-per-pixel
  presets then passed serial geometry/color checks on the two-output
  topology. Device remains on Pos100 (LED 34 red, second output black).
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
| 3 — next | 2D / ledmap through AT and persistence | Configure a mapped panel, recall its scene after reboot, and demonstrate a spatial pattern/effect that proves the mapping. Add the minimum 2D effect needed for this demonstration. |
| 4 | Symmetric mirror per segment | Demonstrate symmetric duplication, distinct from reverse, with documented odd/even-length behavior, AT control and persistence. |
| 5 | Custom effect parameters via AT | Discover labels/usage flags, edit the three existing custom fields, publish and retain them without disturbing unrelated settings. |
| 6 | Expand the effect catalog | Deliver small batches of useful effects with appropriate controls, examples and physical checks; include 2D effects as the panel capability develops. |

The Composer engine already supports 2D/ledmaps/multiple outputs in C++.
The reviewed `adapter_esp_pixels` supports two physical RGB outputs. The
application's schema 2 retains their topology while still reading the existing
schema-1 records. Scenes remain 1D at this stage; the next 2D delivery must
preserve compatibility and existing saved data deliberately.

## Working cycle

One cohesive task -> build -> execute -> fix concrete failures -> local
checkpoint. Reviews focus on behavior and material regressions; secondary
improvements are recorded without creating repeated documentation-only gates.

Claude implements cohesive briefs; Codex reviews, integrates and runs the
bench. Distinguish compilation, automated execution, serial confirmation and
operator visual confirmation. Announce and obtain confirmation before visual
changes; arrange extra output wiring with Rodrigo before touching hardware.
Do not infer push/tag/flash authorization from an earlier delivery.

Next task: [2D canvas / ledmap through AT and persistence](pixels-2d-task.md).
Previous briefs: [multiple physical outputs](pixels-outputs-task.md) and
[named presets](pixels-presets-task.md).
