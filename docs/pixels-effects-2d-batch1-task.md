# Next task: Plasma, MatrixRain and Ripple

## Outcome and ownership

Add the first bounded batch of genuinely 2D effects: Plasma, MatrixRain and
Ripple. This advances the visual catalog without waiting for the EasyLED
backend and reuses the existing ledmap, virtual canvas, `AT+SEGSET2D`, scene
persistence and capability paths. Claude implements and compiles; Codex reviews,
runs QEMU and the WROOM bench with Rodrigo, fixes concrete failures and creates
the local checkpoints.

Layer: external `esp_pixels_fx` for effect implementations and metadata;
Composer/application only for focused integration, capability reporting and
documentation. Do not edit HAL core, EasyLED repositories, transport, storage
schemas, output topology or the web interface.

Before editing, revalidate the applicable `AGENTS.md`, branch, HEAD and working
tree in all three repositories. Read the FX public headers, `fx_xy_gradient.cpp`,
the 2D Composer/AT paths and their READMEs. Preserve unrelated local files and
generated harness artifacts.

## Confirmed baseline

- Application `f78c73a`, branch `checkpoint/wroom-composer-app`.
- Composer `317d510`, sibling `../esp_pixels_composer`, branch
  `feature/at-scene-persistence`, v0.2.3.
- FX `9d8942d`, sibling `../esp_pixels_fx`, branch `integration/v0.2.0`,
  v0.2.4.
- The physical bench is ESP32-WROOM-32 on COM6 with one active GRB WS2812B
  output: GPIO16, 50 pixels. GPIO17 is inactive.

Effects 0..11 are immutable. Meteor/Twinkle/RunningLights passed 8136 combined
checks in QEMU and WROOM, 308 network-contract checks in QEMU, and Rodrigo's
GPIO16/50 visual confirmation. Do not reimplement or renumber them. Claude must
not access hardware or bench backups.

The Composer already supports a real mapped 2D canvas and 2D segments. Scene
schema 4 already persists `is2D`, `{x,y,w,h}`, ledmap identity and map
compatibility. This task consumes those features; it must not create another 2D
abstraction or schema.

## Stable IDs and metadata

Append IDs after RunningLights=11, preserving every existing ID:

| ID | Name | Dimension | Flags | Labels |
|---:|---|---|---|---|
| 12 | Plasma | 2D | SPEED, INTENSITY, PALETTE | speed=Rate, intensity=Scale |
| 13 | MatrixRain | 2D | SPEED, INTENSITY, COLOR1, COLOR2 | speed=Rate, intensity=Tail |
| 14 | Ripple | 2D | SPEED, INTENSITY, PALETTE | speed=Rate, intensity=Density |

All three are `FX_DIM_2D`, so a `Surface` with `height()==1` is rejected. They
must depend on both `x` and `y`, declare a zero `FxWorkDescriptor`, set
`reads_previous_output=false`, use no heap/task/delay/global animation clock,
and redraw every cell on every render. Composer owns ledmap resolution,
brightness, blending, opacity and physical routing.

Use integer/fixed-point arithmetic and existing FX primitives. Do not add
floating point, a noise subsystem, per-cell buffers or shared timing
infrastructure. These algorithms may use FastLED's already-ported MIT color
math primitives, but implement the behavior below directly instead of copying
WLED source.

Each effect begins at deterministic phase zero independent of device uptime.
`speed==0` is a stable frame. For moving states, derive phase from elapsed time
using wide arithmetic so the visible result depends on time, not render
cadence. A uint32 `now_ms` rollover must not panic or access out of bounds.

## Plasma behavior

Plasma is a full-surface palette field formed from three interacting sine
waves. It must visibly vary across both axes even on a nonsquare matrix.

- `speed==0`: temporal phase zero.
- `speed 1..255`: `rate = 4 + ((speed - 1) * 60 / 254)` phase units/second,
  spanning exactly 4..64.
- `intensity 0..255`: `scale = 4 + (intensity * 28 / 255)` phase units/cell,
  spanning exactly 4..32.
- Let `t = elapsed_ms * rate / 1000`, reduced only after wide multiplication.
  For each `(x,y)`, sample the existing `sin8()` at:
  `x*scale + t`, `y*scale - t`, and
  `(x+y)*(scale/2+1) + t/2`.
- Average the three 8-bit samples in a widened sum and use that value as the
  index into `builtinPalette(params.palette)`, at full brightness.

Every coordinate is written. No prior framebuffer content participates.

## MatrixRain behavior

MatrixRain is a deterministic set of independently offset falling heads with
linear tails. Motion is toward increasing `y`; `color[0]` is the head/tail and
`color[1]` is the exact background. Columns differ through a small file-local
integer hash of `x`, without random/global state.

- `speed==0`: deterministic elapsed-zero positions held static.
- `speed 1..255`: `rate = 1 + ((speed - 1) * 31 / 254)` rows/second, exactly
  1..32, accumulated in milli-rows.
- `tail_rows = 1 + (intensity * (height - 1) / 255)`, exactly 1..height.
- The cycle is `height + tail_rows` rows. Each column's head offset is the
  column hash modulo that cycle. Combine the offset and elapsed progress in
  milli-rows, then wrap by `cycle*1000`.
- For each cell, compute the modular distance behind the head. A distance in
  the head cell uses full foreground; the next `tail_rows` cells fade linearly
  to zero; every other cell is exactly background. Define the boundary
  arithmetic explicitly in comments and tests so head/tail endpoints cannot
  be interpreted two ways.

The complete pattern must be reconstructible from `(x,y)`, parameters and
elapsed progress alone. Do not fade or read a previous frame.

## Ripple behavior

Ripple is a concentric palette wave centered on the logical surface. It is a
continuous field, not a particle simulation, so it needs no work buffer.

- Center coordinates use doubled integer space to represent the exact center
  for both odd and even dimensions: compare `2*x` with `width-1` and `2*y`
  with `height-1`.
- Use an integer distance approximation in doubled-coordinate space:
  `major + minor/2`, where `major=max(abs(dx),abs(dy))` and
  `minor=min(abs(dx),abs(dy))`. This depends on both axes and avoids `sqrt`.
- `speed==0`: temporal phase zero.
- `speed 1..255`: `rate = 8 + ((speed - 1) * 120 / 254)` phase units/second,
  exactly 8..128.
- `intensity 0..255`: `spacing = 4 + (intensity * 28 / 255)` phase units per
  doubled-distance unit, exactly 4..32. Higher intensity produces more rings.
- For each cell, `theta = time_phase - distance*spacing` modulo 256. Normalize
  the existing `sin8(theta)` range with the same endpoint discipline already
  used by Breathe/RunningLights. Use the normalized wave as brightness and
  `theta+64` as the selected palette index.

Every cell is redrawn. Width/height 1 combinations must remain safe, while the
registry still accepts the effect only when the actual surface height is >1.

## Integration and persistence

Add one `fx_*.cpp` and one focused permanent FX test file per effect. Register
through the existing IDs/CMake pattern and update the FX README/version to
v0.2.5. `FxInfo` remains the single catalog/metadata authority.

No new AT command, scene field, schema, NVS partition or migration is needed.
Scene/preset maximum record sizes remain 1295 B and 1348 B. Do not add these
2D-only IDs to the existing `effects1D[]` codec fixtures. Instead, extend one
existing 2D round-trip fixture, or add one compact fixture containing IDs
12..14 with valid `h>1` geometry.

Update the application and validation capability arrays/fixture from IDs
0..11 to exactly 0..14 only after the three effects are linked. Do not edit
`easyled-platform`; report that its immutable catalog still needs IDs 3..14.

Keep Composer/application integration deliberately small:

- one real Composer gate using an actual ledmap with a nontrivial mapping and
  one of the new effects, proving both axes reach routed output;
- one AT publish/export/persistence round-trip containing IDs 12..14 and
  distinct parameters;
- one application codec/preset 2D fixture for IDs 12..14;
- the existing capability serialization gate updated to 0..14.

Do not repeat generic ledmap, composition, AT, codec or persistence matrices
for every effect. Their established tests already own those contracts.

## Verification and memory discipline

Each effect's own test file covers metadata, one exact representative frame,
a second timed frame, `speed` 0/1/255, `intensity` 0/255, nonsquare geometry,
small/degenerate safe sizes, full redraw over dirty input, deterministic
restart and a clock-rollover boundary. Combine related assertions rather than
multiplying nearly identical test cases.

The combined scene runner currently uses 178476/180736 bytes of static DRAM,
leaving 2260 bytes. New test scratch belongs on stack or heap, never as large
`static` objects. If the link margin would fall below 1024 bytes, create one
small `pixels_fx_2d_test` runner for the three unit files and retain only the
focused integration gates in `pixels_scene_test`. Do not delete established
coverage or enlarge production task stacks to fit tests.

Build the FX and Composer harnesses, `pixels_scene_test`, the network validation
target and combined network application with ESP-IDF 6.0/current target
defaults. Execute affected QEMU runners, not only compilation. Run
`git diff --check` in every changed repository. Report actual check counts,
image/static-memory sizes and all remaining limitations.

## Proposed bench handoff

Document, but do not execute, a GPIO16/50 transport smoke using a logical
5x10 progressive ledmap on the straight strip:

1. Back up the confirmed scene to `Pos100`; clear/publish the scene.
2. Stage/save `MAPSET=5,10,0,PROGRESSIVE`, reboot as required, and confirm the
   canvas reports 5x10.
3. Show Plasma, MatrixRain and Ripple through `AT+SEGSET2D`, each for about ten
   seconds with visibly distinct palettes/colors and one parameter change.
4. Save/recall one temporary 2D preset and verify reboot recovery.
5. Restore identity mapping and `Pos100`, reboot, and report the final state.

The straight strip proves mapping, transport, timing and persistence but cannot
prove that the spatial shapes look correct as a physical matrix. State that
limitation explicitly. A later real-matrix smoke can close visual geometry
without blocking this implementation/checkpoint. Visual changes occur only
after Rodrigo says he is watching.

## Hardware result — 2026-09-30

Codex flashed the application on the WROOM/COM6 bench with GPIO16 driving 50
pixels and used a progressive 5x10 logical canvas over the straight strip.
Rodrigo confirmed Plasma (Ocean then Heat), MatrixRain (green/dark then
magenta/blue) and Ripple (Party then Ice), including the requested parameter
changes. Each effect remained visible for approximately ten to twelve seconds.
The temporary `Pos100` save, explicit blackout, recall and reboot recovery all
passed. The original NVS and preset partitions were then restored and their
SHA-256 hashes matched the pre-test backups byte for byte.

A separate bench-only autoplay image was subsequently flashed at Rodrigo's
request. Every reset runs the same three-effect sequence and finishes on a live
black Solid segment. Serial evidence ended with
`PIXELS_DEMO: autoplay complete; holding black until reset`. The temporary
autoplay source edit was removed after the image was built; it is not part of
the production source or this task's implementation checkpoint. The saved
bench image has SHA-256
`854a2c18b2a3b747b7d5e01028d361a2851e1bdef08d3e00ef05999978a06ef1`.

This hardware run validates mapping, transport, animation timing, palette and
parameter changes, persistence and autonomous playback on a straight strip.
It still does not validate the perceived geometry on a physically assembled
2D matrix.

## Deferred scope

Do not add Fire2012 or any effect requiring `FxRuntime::work`; Composer working
buffer ownership remains a separate task. Defer extra 2D effects, transitions,
new palettes, remote catalog migrations, matrix hardware provisioning and UI
work. Do not modify EasyLED.

Restrictions for Claude: no flash, serial, hardware access, commit, push, tag,
merge/rebase, release, EasyLED edit or unrelated cleanup. Leave implemented and
compiled working-tree changes for Codex review.
