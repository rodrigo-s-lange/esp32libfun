# Next task: Blink, Breathe and Chase

## Outcome and ownership

Deliver three usable effects through the existing Composer controls and preset
workflow: Blink, Breathe and Chase. This is the next catalog increment toward
the 2026-10-15 delivery. Implement and build; Codex reviews, runs hardware with
Rodrigo observing, fixes concrete failures and creates local checkpoints.

Layer: external `esp_pixels_fx` for effects/metadata; application test wiring
and documentation. Composer changes are limited to focused integration tests
unless a concrete defect is found. No HAL, transport, editor, MQTT or storage
redesign. EasyLED owns its interface/backend and product network orchestration.

Read AGENTS.md, its required docs, the FX public headers/README and existing
effects before editing. Check actual code, not the prior handoff's assumptions.

## Confirmed baseline

- Application `4d8c1ac99ffdefd2ba1c2cb9a607b6fedf00c882`, branch
  `checkpoint/wroom-composer-app`. Later documentation-only commits are expected.
- Composer `d575dbe06ff713a954c13fbac60f58ba43613f4d`, sibling
  `../esp_pixels_composer`, branch `feature/at-scene-persistence`, v0.2.3.
- FX `6a93967c9499f4e72e24b2e21cdad4b3b4428926`, sibling
  `../esp_pixels_fx`, branch `integration/v0.2.0`, v0.2.2.
- Pixels remains `5d7db6b7ce17935ec5213e241c66055787726564` under
  `framework/libs/esp_pixels`. Preserve unrelated local changes/artifacts.

Use the sibling libraries selected by root CMake, not older nested copies.
Do not commit/clean untracked `ci_harness` dependencies, sdkconfig or managed
components. If an obsolete Claude memory says custom parameters are still
uncommitted, verify these commits and retire that stale task memory.

Evidence: 7088 scene-runner +172 adapter checks =7260, zero failures. Rodrigo
confirmed custom controls, Mirror/reverse, crossing both outputs and preset/
reboot recovery. All 100 originals restored and verified. Last state: WROOM/
COM6, GPIO16/50 +GPIO17/12 GRB, IDENTITY, Pos100, LED34 red, brightness96,
GPIO17 black. GPIO4/5 disconnected. Do not access the board or bench backups.

## Effects and controls

Append stable IDs after Stripes=5: Blink=6, Breathe=7, Chase=8. Verify the
registry first; do not renumber IDs 0..5 or change existing effect behavior.

| Effect | Dimension | Controls | Visible behavior |
|---|---|---|---|
| Blink | BOTH | SPEED, COLOR1, COLOR2 | Entire segment alternates the two colors, equal half-cycles. |
| Breathe | BOTH | SPEED, COLOR1, COLOR2 | Entire segment smoothly interpolates from color1 to color2 and back. |
| Chase | 1D | SPEED, COLOR1, COLOR2, CUSTOM1/2/3 | Stripes-style bands move toward increasing source coordinates. |

For Blink/Breathe, speed 1..255 maps monotonically from a 5000 ms cycle to
200 ms: `period_ms = 5000 - ((speed - 1) * 4800 / 254)`, using wide integer
arithmetic. Blink starts with color1; Breathe starts at color1, reaches color2
at half-cycle and returns to color1. Use the existing fixed-point helpers for
a smooth periodic interpolation, no floating point or new math subsystem.
Document quantization and test against the existing helpers' exact rounding.

Speed 0 is an explicitly static state: color1 for Blink/Breathe; Chase is
identical to Stripes for the same colors/custom values. Selecting speed 0 may
reset the visible phase to that static state; do not promise phase continuity
on a speed edit. No palette/intensity/background flags on these three effects.
Publish clear speed labels through the existing metadata mechanism.

Chase uses the same custom labels and raw meanings as Stripes:
Band width=value+1, Gap width=value+1, Phase=0..255 source cells. At animation
phase zero its output equals Stripes. Speed 1..255 maps to 1..64 cells/second:
`rate = 1 + ((speed - 1) * 63 / 254)`. Advance toward increasing x: after one
cell of travel, new[x] equals the prior periodic pattern at x-1. Wrap the
pattern with band+gap; do not truncate the period to the surface width.

Use per-segment FxRuntime and now_ms for animation, never a global/static
animation clock, task, delay, heap allocation, history or working buffer.
Restart begins deterministically; repeated timestamps do not advance motion;
different render cadences reach the same phase for the same elapsed time.
Handle uint32_t clock rollover with unsigned elapsed arithmetic and use wide
intermediates for rates, periods and x+phase. Do not refactor old effects to
share new timing infrastructure. A tiny local helper is sufficient if needed.

Every render fills the entire source Surface. BOTH means every row of a real
2D surface is written, not just its first row. Composer continues to own
Mirror/reverse, segment/global brightness, blend and physical routing.
Breathe's artistic interpolation must not mutate those compositor settings.

## Persistence and integration

No new AT commands, parameter fields, schemas, partitions or migrations here.
Keep schema4, scene1295 B/preset1348 B maximum, 100 positions and 32 segments.
The codec DOES validate effect registration/dimensions/work requirements via
`fx::describe()`; appending registered compatible IDs needs no catalog bump.
Do not recreate the last delivery's bug by selecting 2D-only XYGradient in
an all-1D max-size fixture. Keep the preset-handler stack fix intact: no large
`AtScene{}` temporary/reset on the 4096-byte AT task stack.

For EasyLED integration, add a compact table to the FX README with IDs,
dimensions, supported controls/labels and raw control meanings for these
effects. Use FxInfo as the source; do not build a second metadata registry.
This task does not edit `easyled-platform`, its migrations or its contracts.
Its separate integration work must carry mirror/reverse/custom/color_source
and catalog IDs 3..8 while preserving existing IDs. Preset UUID/version to
local-slot mapping and MQTT/HTTPS integration stay in its existing workstream.

## Verification and handoff

Register permanent tests in a runnable target, preserving all7260 baseline
checks. Cover exact Blink boundaries; Breathe endpoints/intermediate phases;
Chase direction, wrap and non-period-multiple width; speed0 and speed1/255;
custom0/255; width1; actual multi-row rendering for BOTH; complete redraw;
separate runtimes; restart, repeated timestamp and clock rollover.

Add focused integration gates through real effects: Chase with Mirror/reverse
and a two-output crossing; an actual 2D Blink/Breathe segment; AT publish and
preset round-trip retaining the new IDs and distinct parameters. Reuse existing
fixtures rather than repeating the whole AT/persistence suite per effect.

Scene-runner static DRAM headroom is only2208 B. Prefer reused fixtures or
checked heap scratch released between tests; do not remove coverage or expand
core task stacks. Split only the new effect runner if it genuinely cannot fit,
and report its separate build/run command and count. Compilation is not execution.

Build application `build-wroom-rmt-safe`, affected library harnesses and test
runner(s) with ESP-IDF6.0.2 and existing cache-safe RMT settings. Never edit
sdkconfig directly. Run diff --check in changed repos. Report actual image/
static memory sizes, registered checks and any concrete limitations.

Provide a short bench sequence with slow visible Blink/Breathe, Chase speed/
width changes, Mirror, crossing, save BEFORE explicit blackout, named recall
and reboot. Use backed-up Pos100, never a101st name; Codex restores the100
originals and finishes on LED34 red. 2D behavior can be observed on the existing
straight strip as a virtual canvas; a new physical matrix is not required.

Restrictions for Claude: no flash, serial, bench-backup access, commit, push,
tag, merge/rebase or unrelated cleanup. Leave implemented, compiled changes
for Codex. The accepted startup green flash remains non-blocking.
