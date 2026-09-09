# EasyLED / pixels roadmap

Updated with Rodrigo on 2026-09-09. This roadmap belongs to the product
application and external pixels libraries; it does not expand the HAL core.

## Validated baseline

- Composer `97159d21ac5a6f4e1c1a71cc4792baade552b6df`: managed rendering,
  segments, effects, AT composition controls and application-owned persistence.
- Application validation checkpoint `ca596622eb1c5abfd658598471808f21c1362e87`.
- WROOM-32 / COM6 / GPIO16 / 50 WS2812 RGB pixels, GRB, 115200 baud.
- 3785 executed checks passed. Rodrigo confirmed the physical blend sequence,
  reboot appearance and restoration of the saved Rainbow/Sinelon scene.

## Delivery order

The order below is the proposed implementation sequence. Named presets, 2D
and multiple outputs are explicit MVP requirements. Symmetric mirror,
custom parameters and new effects are also in the roadmap; their placement
here does not establish an agreed exclusion from the MVP.

| Order | Delivery | Acceptance result |
|---|---|---|
| 1 — next | Named presets via AT | Save, list, load and delete named scenes; at least two distinct presets survive reboot and can be recalled. |
| 2 | Multiple physical outputs | Configure and drive at least two outputs through the product API, persist their configuration, and validate with separately connected strips. |
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

Next task: [named presets](pixels-presets-task.md).
