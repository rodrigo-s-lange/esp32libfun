# Next task: Meteor, Twinkle and RunningLights

## Outcome and ownership

Add the next three stable 1D effects without waiting for the EasyLED backend:
Meteor, Twinkle and RunningLights. This is a bounded catalog increment toward
the 2026-10-15 delivery. Claude implements and compiles; Codex reviews, runs
QEMU and the WROOM bench with Rodrigo, fixes concrete failures and creates the
local checkpoints.

Layer: external `esp_pixels_fx` for effect implementations and metadata;
Composer/application only for focused integration, capability reporting and
documentation. Do not edit the HAL core, EasyLED repositories, transport,
storage schemas or the web interface.

Read the applicable `AGENTS.md`, required repository docs, FX public headers,
README and the existing effects before editing. Preserve unrelated local files.

## Confirmed baseline

- Application `d111055e6a57edebc45242dd8a544946f0e5f6db`, branch
  `checkpoint/wroom-composer-app`.
- Composer `17b5e9a6ef6e9701b1336bfd9cc03470173c8449`, sibling
  `../esp_pixels_composer`, branch `feature/at-scene-persistence`, v0.2.3.
- FX `9de81da4848df09be3b2c5aa4156a1e160ceb84c`, sibling
  `../esp_pixels_fx`, branch `integration/v0.2.0`, v0.2.3.
- Pixels remains the application-selected library under
  `framework/libs/esp_pixels` and is not expected to change.

Blink/Breathe/Chase IDs 6..8 are already implemented, checkpointed and
validated: 7490 combined checks passed in QEMU and on WROOM, followed by visual
confirmation. Do not reimplement or renumber them. The current physical bench
is ESP32-WROOM-32 on COM6 with one active GRB WS2812B output: GPIO16, 50 pixels.
GPIO17 is physically inactive. Do not access hardware or bench backups during
Claude's implementation.

## Stable IDs and metadata

Append IDs after Chase=8, preserving every existing ID:

| ID | Name | Dimension | Flags | Labels |
|---:|---|---|---|---|
| 9 | Meteor | 1D | SPEED, INTENSITY, COLOR1, COLOR2 | speed=Rate, intensity=Tail |
| 10 | Twinkle | 1D | SPEED, INTENSITY, COLOR1, COLOR2 | speed=Rate, intensity=Density |
| 11 | RunningLights | 1D | SPEED, INTENSITY, COLOR1, COLOR2 | speed=Rate, intensity=Density |

For all three, `color[0]` is the foreground and `color[1]` is the background.
Do not advertise palette, background/color3 or custom controls. All three must
declare a zero `FxWorkDescriptor`, use no heap, task, delay, global animation
clock, history buffer or per-cell working buffer, and redraw their full source
surface on every render.

Use each segment's own `FxRuntime` and unsigned `now_ms` deltas. A restart begins
at deterministic phase zero independent of device uptime. Repeated timestamps
do not advance; different render cadences reach the same state for the same
elapsed time; uint32 clock rollover works through unsigned subtraction. Keep
the small timing implementation local to each effect rather than creating a new
shared timing subsystem or refactoring existing effects.

## Meteor behavior

Meteor is a foreground head followed by a linearly fading tail over the
background. It moves toward increasing source coordinates. Composer continues
to own reverse and mirror.

- `speed == 0`: deterministic static head at source coordinate zero.
- `speed 1..255`: `rate = 1 + ((speed - 1) * 63 / 254)` cells/second, using
  wide integer arithmetic.
- Tail length is relative to the segment:
  `tail_cells = 1 + (intensity * (width - 1) / 255)`. Width one therefore has
  one tail cell for every intensity.
- Track progress in integer milli-cells and wrap it by
  `(width + tail_cells) * 1000`. This lets the head traverse the strip and the
  complete tail leave it before the next cycle.
- Blend background toward foreground according to distance behind the head.
  For signed milli-cell distance `d = head - x*1000`: `-1000 < d < 0`
  uses `(d + 1000) * 255 / 1000`; `0 <= d < tail_cells*1000` uses
  `255 - d*255/(tail_cells*1000)`; every other distance is zero. Thus the head
  is full foreground, brightness reaches zero at the tail boundary, and the
  next cell has a sub-cell ramp at slow speeds.

The output must be reconstructible from runtime progress alone. It must never
depend on pixels drawn by an earlier frame.

## Twinkle behavior

Twinkle is a deterministic field of independently pulsing foreground points
over the background. It must look irregular without a global/random generator:
use a small file-local integer hash of source coordinate for membership, phase
offset and modest period variation.

- `speed == 0`: the deterministic elapsed-zero field, held static.
- `speed 1..255`: base pulse period maps monotonically from 4000 ms to 400 ms:
  `period_ms = 4000 - ((speed - 1) * 3600 / 254)`.
- Per-cell period may vary deterministically from 75% through 125% of the base;
  it must remain nonzero and independent of render cadence.
- Density threshold is a 16-bit value
  `8 + (intensity * 248 / 255)` in the range 8..256. A coordinate is active
  when the low byte of its membership hash is less than that threshold, so the
  top endpoint activates every coordinate exactly.
- Each active coordinate uses a smooth deterministic pulse from background to
  foreground and back, with a hash-derived phase offset. Reuse existing fixed
  point primitives such as `sin8`; do not add floating point or a noise system.
- Inactive coordinates are exactly the background color.

The coordinate membership may remain stable while individual phases and periods
differ. This is intentional for the first bounded Twinkle implementation.

## RunningLights behavior

RunningLights is a spatial sine brightness wave moving toward increasing source
coordinates, blended between the two configured colors.

- `speed == 0`: frozen phase zero.
- `speed 1..255`: temporal phase rate maps from 16 through 512 phase units per
  second: `rate = 16 + ((speed - 1) * 496 / 254)`.
- `intensity 0..255`: spatial step maps from 5 through 64 phase units per cell:
  `step = 5 + (intensity * 59 / 255)`. This ranges from a broad wave to tightly
  spaced waves without depending on segment width.
- For source coordinate `x`, sample the existing 8-bit sine at
  `x * step - time_phase`, normalize its existing asymmetric endpoint range in
  the same disciplined way as Breathe, and blend background to foreground.
  The subtraction is required for a fixed feature to move toward increasing
  source coordinates; Codex caught and corrected the original plus-sign
  implementation during review.

Use wide intermediates before reducing phase modulo 256. The effect is 1D-only;
genuine 2D waves belong to the next task.

## Integration and persistence

Add one `fx_*.cpp` and one focused permanent FX test file per effect, register
them through the existing ID/registration/CMake pattern, and update the FX
README/version to v0.2.4. `FxInfo` remains the authoritative metadata registry.

No new AT commands, scene fields, schemas, NVS partitions or migrations are
needed. Scene/preset records remain schema 4 and maximum sizes remain 1295 B and
1348 B. Extend existing 1D effect-ID fixtures to 9..11 without duplicating the
entire codec suite. Keep the 4096-byte AT task stack fixes intact.

Update the application capability manifest and its provisional expected event
from IDs 0..8 to 0..11 only after the effects are linked into the combined
firmware. Do not edit `easyled-platform`; report that its catalog migration must
append 9..11 later while keeping every earlier ID immutable.

Add only focused Composer/application integration gates:

- one real Meteor scene crossing two fake physical outputs with mirror/reverse;
- one AT publish/export or preset round-trip that retains IDs 9..11 and their
  distinct parameters;
- capability serialization reporting exactly IDs 0..11.

Do not repeat the full composition, AT or persistence matrix for every effect.
Existing generic gates already own those contracts.

## Verification and handoff

FX tests should cover metadata, exact representative frames, speed 0/1/255,
intensity 0/255, width one, complete redraw over dirty input, independent
runtimes, restart, repeated timestamps, cadence independence and uint32 clock
rollover. Share cases inside each focused test instead of multiplying nearly
identical assertions.

The combined scene runner has only about 2168 bytes of static DRAM headroom.
Keep large fixtures off static storage. If the link margin would fall below
1024 bytes, run the three new unit suites in the FX harness or a separate small
runner and leave only the focused integration gates in the combined scene
runner. Do not remove established coverage or enlarge core task stacks merely
to fit tests.

Build the FX harness, Composer harness, `pixels_scene_test` and combined network
application with ESP-IDF 6.0 and existing target defaults. Execute the affected
QEMU runners, not just compilation. Run `git diff --check` in each changed repo.
Report actual test counts, image/static-memory sizes and limitations.

Propose, but do not execute, a short GPIO16/50 bench sequence: slow/fast Meteor
and tail extremes; sparse/dense Twinkle; broad/tight RunningLights; one mirror
interaction; temporary named preset recall and reboot recovery. The local preset
slots are a disposable cache, but delete the temporary test entry and leave a
documented final state. Visual changes occur only after Rodrigo says he is
watching.

## Deferred scope

Fire2012 is deliberately separate because Composer currently rejects effects
with a nonzero working-buffer descriptor. Do not silently add Composer working
buffers here. The following task will cover genuine 2D effects such as Plasma,
MatrixRain and Ripple after this 1D batch is checkpointed.

Restrictions for Claude: no flash, serial, hardware access, commit, push, tag,
merge/rebase, release, EasyLED edit or unrelated cleanup. Leave implemented and
compiled working-tree changes for Codex review.
