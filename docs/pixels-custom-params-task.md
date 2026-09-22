# Next task: custom effect parameters through Composer AT

## Deliverable and baseline

Deliver usable controls for the three existing `FxParams::custom[]` bytes:
discover their labels, stage values through AT, publish, save/recall a preset
and retain them after reboot. Include one small production effect that actually
uses the controls, since none of the current effects consumes them. Keep this
one cohesive increment; do not expand the effect catalog beyond that consumer.

Claude implements and builds. Codex reviews, arranges the hardware observation,
runs the bench, fixes concrete failures and creates the local checkpoint.
Implement the brief directly; report concrete blockers, not another design plan.

Code baselines (later documentation-only commits are acceptable):

- Application `b6d78042d5992d157811437e3822a6ba02fe84c1`, branch
  `checkpoint/wroom-composer-app`.
- Composer `c2c3e542ce00b60c9ae4c7d224244676bb8a2de1`, sibling
  `../esp_pixels_composer`, branch `feature/at-scene-persistence`.
- FX `4e977137950a30c81f4151b411b1541fba85adb4`, sibling
  `../esp_pixels_fx`, branch `integration/v0.2.0`.
- Pixels `5d7db6b7ce17935ec5213e241c66055787726564`, nested
  `framework/libs/esp_pixels`; preserve unrelated local changes.

Read AGENTS.md and its required documents, then the relevant public headers,
READMEs, Composer AT setters/query code, FX registry/effects and app codecs.
Verify working trees before editing. Use the sibling Composer/FX selected by
root CMake, not older nested copies or `pixels_composer_light_test`. Preserve
the unrelated untracked Composer `ci_harness/dependencies.lock`.

Baseline evidence: 6760 hardware checks passed (6588 scene runner +172 adapter).
Mirror even/odd, reverse, two-output routing and preset/reboot recovery were
visually confirmed. WROOM/COM6: GPIO16/50 and GPIO17/12 WS2812 RGB, both GRB.
Last state: IDENTITY, Pos100, physical LED34 red, brightness96, GPIO17 black;
all Pos001..Pos100 occupied and restored. Do not access or change the board.

## Scope and AT contract

Layer: external Composer AT sidecar for controls/discovery; external FX for
one consumer. Application changes should be limited to test integration and
documentation unless an actual defect requires a fix. No HAL/transport or
Composer rendering-engine changes are expected. Leave the standalone FX-player
AT sidecar outside this increment; the product uses per-segment Composer AT.

Reuse `FX_USES_CUSTOM1/2/3`, `FxInfo::custom_labels[3]` and `FxParams::custom[3]`.
Do not add another metadata system, parameter storage, JSON protocol or editor.

- Register `AT+SEGCUSTOM1=<id>,<0-255>`, `AT+SEGCUSTOM2=<id>,<0-255>` and
  `AT+SEGCUSTOM3=<id>,<0-255>`. Public numbering 1..3 maps to array slots 0..2.
- Follow the existing SEGSPEED/SEGINT staged-setter pattern. Strictly parse
  nonzero ID, value and end of input; reject negatives, overflow, extra fields,
  unknown IDs and unsupported controls before any mutation.
- Require the selected staged effect's corresponding CUSTOM flag. A label
  alone never enables a control; a flag without a label is still usable.
- Each setter changes exactly its selected byte. Do not change another custom
  byte, color source, colors, speed/intensity, geometry, mirror/reverse, state,
  brightness/blend/opacity, restart token or global settings.
- Setters do not render or write NVS. Only the existing confirmed publish
  path makes the edit live and durable. Preserve cancellation/timeout and
  applied-but-not-saved retry semantics.
- `AT+SEG=<id>?` shows only supported custom setters, their current staged
  values and nonempty labels via the existing `writeSegControl()` convention.
  A custom-only effect must not report `OPTIONS=none`. Query/help do not write.
- Availability follows the currently staged effect, including after replacing
  that effect via SEGSET/SEGSET2D. Preserve stored custom bytes on replacement
  just as existing controls are preserved. Do not erase an unsupported slot;
  it remains data even while the new effect does not expose its setter.
- These setters are shape-neutral: allow a 1D or 2D staged segment whenever
  its effect declares the flag. Do not add a 1D restriction merely because the
  demonstration effect below is 1D. Existing geometry/dimension checks stay.

Keep three thin command wrappers over shared validation if that fits the
existing style; no generic command framework refactor. Registration grows
23 ->26 Composer commands; update counts, discovery and public usage docs.

## One concrete consumer: Stripes

Add `Stripes` as the next stable FX ID (5 after XYGradient; verify actual IDs).
This is a static two-color 1D band pattern, useful for layout/segment checks.
It must not change any existing effect, default parameter or saved meaning.

Declare `FX_DIM_1D`, no history or work buffer, COLOR1/COLOR2 and all three
CUSTOM flags; do not advertise speed/intensity/palette. Use labels:

- custom[0]: `Band width`; actual width = value+1 cells (1..256).
- custom[1]: `Gap width`; actual width = value+1 cells (1..256).
- custom[2]: `Phase`; offset = value cells (0..255), wrapping with the period.

For source coordinate x, period = band+gap:

```text
((x + phase) % period) < band ? color[0] : color[1]
```

Use adequately wide unsigned intermediates: period can be 512 and x+phase
must not wrap at 8 or 16 bits. Fill the whole source Surface every render;
use the existing effect registration/build pattern and normal frame interval.
No heap allocation or clock dependency. Composer remains responsible for
mirror/reverse, brightness, blend and routing. No 2D version in this increment.

Example for the bench: custom values 1,2,0 mean two primary pixels followed
by three secondary pixels, repeating. Changing just phase to1 shifts that
pattern by one source cell. Values stored and queried remain raw 0..255;
document the +1 interpretation instead of inventing extra units in the parser.

## Persistence and memory

All three custom bytes are already present in scene schemas1..4 and presets.
Keep schema4 and the existing record layout: scene1295 B, preset1348 B maximum,
100 positions, 32 segments, unchanged partitions. Do not introduce schema5,
new keys, migration or a catalog-version bump merely for an appended FX ID.
Retain old effect IDs and schema1/2/3 readers. Verify the existing catalog
compatibility policy against the actual code before updating version docs.

Confirmed-scene export/load, pause-triggered saving, preset save/load and
deduplication must retain all three bytes. Never save unpublished custom edits
through a different command. Failed decode/load must preserve its outputs and
the staged/live scene; unsupported fields in old records are not malformed.

Keep the Mirror fixes: preset handlers use static scratch scenes without a
whole `AtScene{}` reset temporary. That temporary overflowed the 4096-byte AT
task stack during real 32-segment legacy recall; compiled handler frames are
now48 B. Do not reintroduce large stack temporaries or increase core stack size
as a substitute for correctly placed application storage.

The last scene-suite static DRAM margin was2224 B (98.77% used). Reuse test
fixtures or checked temporary heap allocations, released between cases. Split
a runner only if necessary; never drop baseline coverage to make the build fit.

## Focused tests, builds and handoff

Keep all6760 baseline checks and register additions in permanent runnable
firmware. Tests must exercise the actual AT path and actual Stripes rendering:

- All three slots separately, boundaries0/255, staged-only before publish and
  expected pixels after publish. Check unrelated segment fields remain intact.
- Invalid/missing/extra arguments, numeric overflow, unknown ID, absent flag,
  and effect changes. Discovery: supported/unsupported slots, distinct labels,
  null/empty labels and a custom-only fixture. Use existing output-capture
  support if available; do not claim printed-text assertions from feedLine's
  dispatch status alone. A test-only 2D consumer proves shape-neutral setters.
- Stripes exact output for unequal bands/gaps and phase shifts; all0/all255
  controls, source width1 and a non-period-multiple width; complete redraw and
  time independence. Verify palette/speed flags are absent.
- One composition integration check with Mirror/reverse and one crossing the
  FakeSink output boundary, proving settings reach the real effect normally.
- Confirmed publish/export, preset save/load, simulated reboot, unchanged-save
  deduplication and failed-save retry with distinct custom bytes. Preserve
  unpublished edits across pause-save behavior and old-schema custom values.
  Existing maximum-size round trips must retain1295/1348 B bounds.

Build app `build-wroom-rmt-safe`, expanded `pixels_scene_test`, independent
adapter runner and affected library harnesses using ESP-IDF6.0.2. Keep RMT TX
ISR cache safety and main/README.md's build setup; never edit sdkconfig directly.
Report actual binary/static memory sizes and any runtime allocation changes.
Run git diff --check in each changed repo. Update relevant headers/READMEs,
metadata/control documentation and runner registration. No cosmetic refactor.

Handoff: changed files/repos, builds, sizes and runnable coverage; a short
AT sequence on the existing strips showing each custom control independently,
Mirror interaction, a crossing segment, explicit blackout/preset recall and
reboot. Use a backed-up existing position such as Pos100, never a101st name.
Codex will arrange authorization/observation, verify fresh backups, execute
the suites and restore all100 originals, ending on Pos100.

Claude restrictions: no flash, serial, bench-backup access, commit, push, tag,
merge, rebase or unrelated cleanup. Leave changes in the working trees for
Codex. Compilation is not hardware execution. Startup green flash remains an
accepted non-blocking issue; do not reopen it. Broader effect expansion stays
in the next roadmap delivery.
