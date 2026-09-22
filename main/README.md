# WROOM-32 pixels AT bench

The root application drives WS2812 pixels on one or more independently
configured GPIO outputs; the fixed boot default remains 50 pixels in GRB
order on GPIO16 unless a second/different output has been saved via
`AT+OUTSET`/`AT+OUTSAVE` -- see "Multiple physical pixel outputs" below.
At application startup it initializes every configured sink and sends a
completed black frame on each after reading output configuration and before
reading scene/preset NVS. The persistence bridge
renders the recovered scene once during attachment; the managed task later
continues rendering. This also clears every strip when booting with
no valid saved scene; a runtime empty publication still behaves as below.
`Composer::start()` owns rendering; `app_main()` returns after setup. The
adapter and Composer have static lifetime. Boot restores the last saved scene
from NVS before starting the managed task and AT reader. With no valid saved
scene it starts empty. Storage belongs to this application; the Composer
sidecar only has an optional, storage-neutral scene callback.

The `PIXELS_COMPOSER` sidecar is enabled. Discover it with `AT+HELP?`,
then `AT+PIXELS_COMPOSER?`. `AT+SEG?` inspects the staged table and
`AT+COMPAUSE?` reports managed-runtime state. Changes made with segment
setters take effect only after `AT+SEGPUB`. `PIXELS_PRESETS` (unreleased)
is a second, application-owned AT library for named
presets -- see its own section below.

## Automatic configuration retention (schemas 1/2/3 read, schema 4 written)

- A confirmed `AT+SEGPUB` saves the entire ordered scene. `AT+COMPAUSE=0|1`
  saves global pause with the last confirmed segments, never intervening staged
  edits. Queries and segment setters never write NVS.
- Retained: IDs, start/stop, effects, reverse, segment state, brightness, blend,
  opacity, speed, intensity, palette, color source, all three RGB colors, all
  three custom parameters, restart tokens, global brightness and global pause.
- Output topology (pins/counts/wire order, 1..`kMaxOutputs` slots) is embedded
  in every record's own header as of schema 2 -- see
  "Multiple physical pixel outputs" below for how a record's own topology is
  checked for compatibility against the device's current one before it is
  ever applied. Schema 3 adds the canvas descriptor and 1D/2D segment shape;
  see the 2D section below for layout compatibility.
- Not retained: animation time, effect runtime, pixel/history buffers or task
  handles. Effects restart on boot. Paused segments/global pause restore as
  paused with initially black buffers until resumed; this is configuration
  restoration, not framebuffer restoration.
- Records use explicit little-endian fields, schema/catalog versions, a 64-bit
  generation and CRC32, not raw C++ struct bytes. Schema 1 (single output
  only, fixed 32-byte header) is still read on boot/recall, never written
  again. Schemas 2 and 3 remain readable too. Every new save writes schema 4
  (variable-size header, 38 bytes at one output, up to 43 at two outputs).
  Schema 4 adds mirror in the existing flags byte, keeping record sizes
  unchanged. See the mirror section for its compatibility rules.
- Namespace `pixels_scene`, keys `scene_a`/`scene_b` in the existing NVS
  partition. Save targets the inactive key, commits and reads back; it does not
  overwrite the last valid key. Boot selects the newest valid COMPATIBLE
  record (see below); a torn/corrupt/incompatible latest record can fall back
  to the preceding one. Redundancy within one partition does not protect
  against loss of both records or total flash failure. No automatic NVS erase
  is used, and no record is ever bulk-rewritten at boot as a migration --
  schema 1 records stay schema 1 (and stay readable) until their position is
  next saved by this application.
- Identical canonical configurations are not rewritten. Saves run synchronously
  in the AT task, never per-frame or in the Composer render task. External
  automation should group edits into deliberate publishes to limit flash wear.
  Flash access can still briefly stall rendering on ESP32. The WROOM
  application enables `CONFIG_RMT_TX_ISR_CACHE_SAFE=y` so an in-flight
  WS2812 transmission keeps receiving symbols during cache-disabled NVS
  operations. Placing the handler/encoder in IRAM alone does not enable
  its interrupt during those operations.
- `CONFIG saved` precedes normal success only after storage succeeds (or the
  same record was already saved). Failure reports `applied; NOT saved`; live
  state is not rolled back. Retry `AT+SEGPUB`, which republishes the **current
  staged table**. An indeterminate publish blocks pause-triggered saving until
  a subsequent confirmed publish.
- Clearing is durable: `AT+SEGCLR` then `AT+SEGPUB`. The existing Composer
  empty-scene behavior does not transmit black; use Disabled segments for an
  immediate physical blackout.

Boot reconstructs live and staged tables, preserving the segment IDs for later
queries/setters. Substantial storage buffers and the borrowed confirmation cache
have static lifetime, outside the small main/AT task stacks. Rendering remains
internal to `Composer::start()`.

The callback extension is committed locally as `a39260dff35038a0ec6347e918c340edff98b3ae`
on Composer branch `feature/at-scene-persistence`; it is **not** part of
published `v0.2.0` yet, and it has not been pushed or tagged.

## Dependency baseline

The root CMake selects each pixels component explicitly so the older nested
Composer/FX checkpoints are not accidentally compiled alongside the new line.

| Component | Checkout | Validated revision |
|---|---|---|
| Composer and its adapter | `../esp_pixels_composer` | `c2c3e542ce00b60c9ae4c7d224244676bb8a2de1` on `feature/at-scene-persistence`: two outputs, 1D/2D composition, scene persistence and symmetric mirror via `AT+SEGMIRROR` (v0.2.2); local, unreleased checkpoint |
| FX | `../esp_pixels_fx` | `4e977137950a30c81f4151b411b1541fba85adb4` on `integration/v0.2.0`, including validated `XYGradient` (v0.2.1); local, unreleased checkpoint |
| Pixels transport | `framework/libs/esp_pixels` | `5d7db6b7ce17935ec5213e241c66055787726564`, with existing local README/AT changes preserved |
| Core | this repository's `framework/core` | current application checkout |

These are recorded baselines, not automatic Git revision locks. CMake cache
variables `ESP_PIXELS_COMPOSER_DIR` and `ESP_PIXELS_FX_DIR` can select other
checkouts; changing them requires revalidation. The application prints library
versions at boot to identify the compiled APIs.

## Build and flash

Activate ESP-IDF 6.0.2 in PowerShell, then from the repository root:

```powershell
idf.py -B build-wroom-rmt-safe -D SDKCONFIG=build-wroom-rmt-safe/sdkconfig -D IDF_TARGET=esp32 build
idf.py -B build-wroom-rmt-safe -p COM6 flash
```

Close other serial monitors before flashing. Console baud rate is 115200.
The separate build directory preserves the preceding bench firmware artifacts
and generates its configuration from defaults. An existing `sdkconfig` can
retain the old disabled `CONFIG_RMT_TX_ISR_CACHE_SAFE` value despite new
defaults; verify the generated configuration enables it before flashing.
Wait for the `PIXELS_AT` ready line before sending commands. The Composer
sidecar owns configuration and the adapter owns transport; do not enable the
Pixels or standalone FxPlayer sidecars on this same output.

## Bench verification (2026-09-08)

Built and flashed on WROOM-32/COM6 with ESP-IDF 6.0.2. Boot confirmed
`Composer=v0.2.0 FX=v0.2.0 Pixels=v0.1.0 GPIO=16 count=50 GRB commands=16/48`
and return from `app_main()`. Serial queries confirmed `AT` responds `OK`,
`AT+HELP?` lists only core commands and `PIXELS_COMPOSER`, the library query
lists all 16 commands, `AT+SEG?` reports `staged=0`, and `AT+COMPAUSE?`
reports `paused=0 running=1`. No effect was published in this verification;
the operator subsequently confirmed Rainbow on pixels 0..24 and Sinelon on
25..49. Persistence requires its own validation.

### Persistence gate run — 2026-09-08

The scene test image ran on WROOM-32/COM6: 1605 Composer + 817 AT + 1177
codec/journal checks, **3599 total, zero failures**. NVS was mocked only in that
test image; it did not touch stored device data. The root application was then
rebuilt and flashed with real NVS, returning from `app_main()` with 16/48 AT
commands and `persistence=scene-v1`. Root binary: 227824 bytes, 78% app partition
free. These changes were local and uncommitted at the time of this bench run;
they were since committed locally as this application's
`de582eb9c5639152de90672b1cc98234b7dfa166` and the Composer bridge's
`a39260dff35038a0ec6347e918c340edff98b3ae` (still no tag or push).

Real NVS smoke test passed with the empty scene: first SEGPUB wrote slot A,
generation 1, 36 bytes; identical republish performed no further write. After
`AT+RESET`, boot loaded generation 1 and both AT queries confirmed zero staged
segments and `paused=0 running=1`. Capture:
`build-wroom-v020/persistence-empty-reboot.log`.

The two-effect scene was then confirmed. `AT+SEGSET=1,0,25,2` (Rainbow) and
`AT+SEGSET=2,25,50,1` (Sinelon), with speed/intensity 128 and palette 0 on
both, were staged and published: `AT+SEGPUB` saved slot 1, generation 2, 104
bytes, 2 segments, and returned `CONFIG saved`. Capture:
`build-wroom-v020/persistence-two-effects-publish.log`. After `AT+RESET`, boot
restored slot 1, generation 2, 2 segments, `paused=0`; `AT+SEG?` confirmed
`staged=2` with id=1 Rainbow on `[0,25)` and id=2 Sinelon on `[25,50)`,
`AT+SEG=1?`/`AT+SEG=2?` confirmed speed=128/intensity=128/palette=0(Rainbow)
on both, and `AT+COMPAUSE?` confirmed `paused=0 running=1`. Capture:
`build-wroom-v020/persistence-two-effects-reboot.log`. The operator visually
confirmed Rainbow on pixels 0..24 and Sinelon on 25..49 after the reboot, and
repeated a manual reset with the same result. This is a manual-reboot
configuration-recovery test; it is **not** a test of power loss during a
write, which this firmware has not attempted.

## Composition controls via AT (brightness/blend/opacity) — unreleased

`AT+COMBRIGHT?`/`AT+COMBRIGHT=<0-255>` (global brightness, immediate, the
same pattern `AT+COMPAUSE?`/`AT+COMPAUSE` already has) and
`AT+SEGBRIGHT=<id>,<0-255>`/`AT+SEGBLEND=<id>,<REPLACE|ADD|ALPHA>`/
`AT+SEGOPACITY=<id>,<0-255>` (per-segment, staged, effective only after a
confirmed `AT+SEGPUB`) are new in the Composer sidecar, unreleased over
published `v0.2.0`. These changes are committed locally as
`97159d21ac5a6f4e1c1a71cc4792baade552b6df`, layered on top of the persistence
bridge commit `a39260dff35038a0ec6347e918c340edff98b3ae`
(this application's own `main/main.cpp`/`scene_store`/`scene_codec` are
unaffected -- `SegmentSpec` already carried these fields).
Sidecar command count: 16 -> 21. Before this addition, global brightness had
**no** AT setter at all -- only global pause did (`AT+COMPAUSE`); the two
must not be conflated. `AT+SEG=<id>?` now always shows the three per-segment
controls, for any effect. All four integrate with the existing NVS
persistence: `SegmentSpec` already carried brightness/blend/opacity before
this addition, so no codec or schema change was needed to retain them; a
confirmed `AT+COMBRIGHT` saves exactly like a confirmed `AT+COMPAUSE`
already does. See the Composer's own `README.md`/`esp_pixels_composer_at.cpp`
for the full contract.

### Bench run — 2026-09-09

The expanded `pixels_scene_test` firmware ran on WROOM-32/COM6: **3785
checks, zero failures** (1605 Composer, 1003 AT, 1177 codec/journal).
A quantization-sensitive test assertion was corrected after the first run;
production code was unchanged. See the test project's README for evidence.

The application was then flashed again (229536 bytes, 78% partition free).
Only queries were sent after restoration: discovery listed all 21 commands,
the saved Rainbow `[0,25)` and Sinelon `[25,50)` segments were present,
both Active with brightness=255/blend=REPLACE/opacity=255, global brightness
255 and `paused=0 running=1`. Capture:
`build-wroom-v020/composition-controls-app-restored.log`.

The operator-approved composition-control sequence then ran on the physical
strip: global brightness 64/255, segment 1 brightness 64, and temporary Solid
segments 101 (red) and 102 (blue), both `[0,50)`, over the original scene.
The top layer exercised Replace with opacity 0, Add with opacity 0/255,
and Alpha with opacity 128. Every publication/save returned success.

An `AT+RESET` without resending configuration recovered generation 12:
four segments, global brightness 96, top-layer brightness 192, Alpha and
opacity 128. Post-boot queries confirmed all four values and running state.
The temporary segments were then removed and the original two-effect scene
saved as generation 14 (104 bytes): both segments Active, brightness 255,
Replace, opacity 255, global brightness 255 and pause off. Final queries
confirmed the original geometry, effects, speed/intensity 128 and palette 0.
Capture: `build-wroom-v020/composition-controls-visual.log`.
Rodrigo confirmed the demonstrated color/blend transitions, the same mixture
after reboot, and the final Rainbow/Sinelon restoration visually. Serial
behavior, real-NVS reboot recovery and that visual sequence passed. This was
not a power-cut-during-write test.

## Named presets (100 positions) — unreleased

`PIXELS_PRESETS`, a second, application-owned AT library (`main/preset_codec.*`,
`main/preset_store.*`, `main/presets_at.*`), separate from `PIXELS_COMPOSER`:
discover it the same way, `AT+HELP?` then `AT+PIXELS_PRESETS?`. Four
commands. Save/list/delete affect only named presets; load also updates the
independently retained current scene after confirmed application:

- `AT+PRESETS?` -- lists occupied positions: `PRESET name=<name>
  segments=<n>` per line, then `PRESETS occupied=<n>/100`. Query only.
- `AT+PRESETSAVE=<name>` -- exports the Composer's own **confirmed** scene
  (`esp_pixels_composer::exportConfirmedScene()`) and saves it under
  `<name>`. Never an unpublished staged edit. An existing name is replaced
  deliberately, at its own position; the prior valid record survives any
  write/commit/verify failure. A brand-new name claims the first free
  position; all 100 occupied by other names reports failure, no data lost.
- `AT+PRESETLOAD=<name>` -- validates the whole named preset before
  touching anything, then applies it via
  `esp_pixels_composer::loadConfirmedScene()`: replaces the staged table and
  the active scene, including global brightness/pause, and **discards any
  pending staged edit** -- there is no partial merge. A
  missing/corrupt/incompatible preset leaves the active scene and staged
  table completely unchanged. On a confirmed load, the resulting scene is
  also saved through the existing current-scene path (`CONFIG saved`/
  `applied; NOT saved`, same as a confirmed `AT+SEGPUB`) so it survives the
  next reboot too -- a save failure there is reported only that way; it
  never means the load itself failed, since the scene was already applied.
- `AT+PRESETDEL=<name>` -- erases the named position. Never touches the
  active scene, and never touches the independently retained current scene
  (`pixels_scene` namespace) either.

Names: 1-31 printable ASCII bytes (0x20-0x7E, no control characters),
leading/trailing whitespace trimmed; matching is exact, case-sensitive.

Runtime loads preserve matching segment identities under the existing
Composer carry-forward rules; changed identities/effects/geometry/restart
tokens start fresh. Global brightness/pause are applied after adoption, so
an adoption frame may render before global pause takes effect. Boot still
starts fresh runtimes. Presets do not restore framebuffers.

### Flash layout and capacity (reported before any bench flash)

The standard partition table's `nvs` partition is only 24 KiB -- far too
small for 100 named positions. A new, separate `presets` partition
(`partitions_wroom_presets.csv`, `CONFIG_PARTITION_TABLE_CUSTOM` in
`sdkconfig.defaults.esp32`, never a direct `sdkconfig` edit) adds an NVS-type
partition sized for this, while the existing `nvs`/`phy_init`/`factory`
partitions keep their exact prior offsets and sizes -- the current-scene
data already saved there is untouched:

```
nvs,      data, nvs,     0x9000,  24K
phy_init, data, phy,     0xf000,  4K
factory,  app,  factory, 0x10000, 1M
presets,  data, nvs,     0x110000,320K
```

(Confirmed by building this application and dumping the generated
`partition-table.bin` -- offsets are ESP-IDF's own auto-computed, sequential
ones; only `presets` is new.) Total used: 0x160000 bytes (~1.375 MiB) of the
4 MiB flash.

Capacity math: each preset record is at most 1177 bytes
(`kPresetRecordBytes` -- a 32-segment scene, the same maximum the
current-scene schema already allows, wrapped with a name/header/CRC by
`preset_codec.hpp`). ESP-IDF 6.0.2 uses a blob index entry plus a header for
each data chunk. An unsplit record uses 1 + 1 + ceil(1177/32) = 39 entries
(1248 bytes). Allowing a split across two pages gives up to 41 entries
(1312 bytes), including chunk headers and payload rounding. 100 positions
with both A/B records therefore require up to **262400 bytes** of record
entries. The 320 KiB partition has 80 pages, each with 126 usable 32-byte
entries: 10080 entries versus 8200 for those records, leaving 1880 entries
for namespace metadata, free-page reserve, fragmentation and updates.
This is a sizing estimate; the real-NVS capacity run below also passed.
The RAM mock exercises 100 small scenes, not NVS page allocation. Empty
positions store nothing at all -- 100 is a logical capacity ceiling
enforced by this application, not 100 pre-allocated records.

### Status

On 2026-09-10 the expanded suite ran on WROOM-32/COM6: **5224 checks,
zero failures** (1605 Composer, 1053 AT, 1177 codec/journal, 1389 presets).
See the scene test README for the capture and the corrected test-only
read-fault sequencing failure from the first run.

The application was then flashed with the custom partition table (245840
bytes, 77% app partition free). Serial confirmed the original Rainbow/Sinelon
scene, global brightness 255, `paused=0 running=1`, all four preset commands
and an empty store (`0/100`). Existing current-scene NVS data survived the
partition-table update. Capture: `build-wroom-v020/presets-bench.log`.
Rodrigo confirmed red/blue preset recall and operation after reboot, then
reported intermittent flicker. No unexpected reset appeared in the captured
100-load batch; the visual flicker remains under investigation.

Real NVS accepted 100 maximum-size presets (32 segments, 1177 bytes each),
rejected a new 101st name with `ESP_ERR_NO_MEM`, replaced all 100 while full
(both A/B records populated), and replaced the final name again. After a
software reboot, all 100 remained listed and all 100 loads succeeded. This
exercised 201 maximum-size record writes, including replacement with both
journal sides populated. It does not simulate power loss during a write.
The serial harness's 1500 ms boot wait was too short with the full store;
subsequent queries succeeded. Future bench reset steps allow 5000 ms.

At Rodrigo's request the temporary capacity names were replaced by
`Pos001` through `Pos100`: positions 1/2/3 show only physical LED 1 in R/G/B,
4/5/6 show LED 2 in R/G/B, and position 100 shows LED 34 red. All other
pixels are black; global brightness is 96/255. All 100 saves succeeded.
Rodrigo observed brief flashes on pixels after the selected LED, followed
by stable output. The initial build had `CONFIG_RMT_TX_ISR_CACHE_SAFE`
disabled. The corrected application enables that option in
`sdkconfig.defaults.esp32`; it does not change the HAL or pixel libraries.
The fresh `build-wroom-rmt-safe/sdkconfig` differs from the preceding active
configuration only by this enabled option. Build passed (246096 bytes,
77% app partition free), flash verification passed, and boot recovered all
100 presets and the current LED-34 red scene. The generated partition table
is byte-identical to the preceding preset build.

With LED 34 held red, ten replacements of `Pos100` succeeded before the
correction and ten after it. The subsequent RGB recall sequence loaded all
100 presets and verified each marker's geometry, RGB value and Active state
through AT, with no mismatch. A final `AT+RESET` recovered current-scene
generation 221 (32 segments) and all 100 presets. Operator
confirmation of the corrected preset-transition behavior remains pending. Build/flash
captures: `build-wroom-v020/rmt-safe-build.log` and `rmt-safe-flash.log`;
serial capture remains `build-wroom-v020/presets-bench.log`.

On 2026-09-11 Rodrigo manually reset the board and confirmed LED 34 red,
but reported LED 37 briefly green. He clarified that this happens **once
during reset**, not repeatedly during steady operation. Read-only AT checks
confirmed the expected full-strip black Replace layer and LED-34 red marker,
brightness 96 and running state. A temporary diagnostic disabled the two
underlying animation segments; `PRESETLOAD=Pos100` then restored the exact
preset (current-scene generation 223). No preset was overwritten or deleted.
Rodrigo also reproduced the green flash when switching the power off/on.
The reset transient remains open. The persistence bridge already renders
once during attachment, before scanning presets; the roughly 1.4 s boot
time is time to AT readiness, not the time of the first transmission.
An application-only change now initializes
the sink and transmits a black frame before reading either store, using the
existing begin/submit/commit/wait API before any render task exists. It does
not save that temporary black frame. Build and flash passed (246512 bytes,
76% app partition free). Three software resets recovered the unchanged
current-scene generation 223 and all 100 presets; final AT queries confirmed
the LED-34 red marker, brightness 96 and running state. Captures:
`build-wroom-v020/startup-black-build.log`, `startup-black-flash.log` and
the shared serial log. Rodrigo confirmed the green flash still occurs on
manual reset with this build. This initialization
cannot control the data line before `app_main()` starts, and the origin of
the green transient is not yet established.

Rodrigo explicitly accepted continuing on 2026-09-11 with this startup
flash unresolved. It is a known non-blocking bench issue for this checkpoint,
not a confirmed strip defect or a claimed firmware fix. The 100 named
presets, capacity/replacement/reboot tests and automated checks are retained
as completed evidence. Proceed to multiple physical outputs; do not reopen
the startup investigation as a prerequisite without new evidence or request.

## Multiple physical pixel outputs — validated locally, unreleased

Claude implemented this brief (`docs/pixels-outputs-task.md`) against app
baseline `08966f65f297bf62b73a10d04f1b0b1ff01087f6` and Composer baseline
`0730626d2842a55d980b0848e636b579ecc81c62`, with no Composer core change
(its existing multi-output-slot `build()`/`renderFrame()` support already
covered this; only `adapter_esp_pixels` and this application needed new
code). Codex reviewed the integration and executed the automated hardware
gates on 2026-09-14. The application now boots with GPIO16/50 and GPIO17/12
GRB outputs. Rodrigo confirmed the two colors, the segment crossing the
output boundary, preset recall after explicit blackout and reboot recovery.

### Adapter: `EspPixelsSink::kMaxOutputs == 2`

`adapter_esp_pixels` now drives up to `kMaxOutputs` (2) independently wired
`esp_pixels::Pixels` instances/RMT channels, one per output slot. `init()`
is unchanged (signature and behavior) and always configures slot 0; the new
`addOutput()` appends slot 1 (and so on, up to `kMaxOutputs`). This is this
adapter's own delivered ceiling for this cut, distinct from two other
numbers that must never be conflated with it: `esp_pixels_composer::
MAX_OUTPUTS` (8, a software table size only) and the ESP32 classic's own 8
total hardware RMT TX channels, shared with anything else in the build using
RMT. Rejects a duplicate `Pixels` instance or GPIO pin before any mutation;
on a partial `addOutput()` failure only that attempt's own (not yet
committed) resources are released, every already-configured slot is
untouched. `commit()`/`wait()` start and drain every configured slot
regardless of an earlier slot's own failure -- an in-flight buffer is never
reused or freed while its own transmission might still be reading it; `end()`
tracks per-slot drain state across retries so a failed `end()` can be
retried safely without re-waiting an already-drained slot. `synchronized_start`
stays `false` (not implemented). See `adapter_esp_pixels/include/
esp_pixels_composer_adapter_esp_pixels.hpp` and `adapter_esp_pixels/test/
test_adapter.cpp` (172 checks passed on-target) for the full contract
and its gates.

### Output topology AT library (`PIXELS_OUTPUTS`) — unreleased

A new, application-owned AT library (`main/output_config.hpp`,
`main/output_store.*`, `main/outputs_at.*`), entirely separate from
`PIXELS_COMPOSER`/`PIXELS_PRESETS`: discover it the same way, `AT+HELP?`
then `AT+PIXELS_OUTPUTS?`. Output pins/counts/wire order are
application/device configuration, deliberately separate from effect
segments and from the transport-neutral Composer.

- `AT+OUT?` -- lists ACTIVE, SAVED (next boot) and STAGED output configuration,
  one `OUT active|saved|staged slot=<n> pin=<p> count=<c> order=<order>`
  line per configured slot, then `OUT active=<n> staged=<m>`. Query only.
- `AT+OUTSET=<slot>,<pin>,<count>,<order>` -- stages one output slot in the
  candidate (`slot` 0..`kMaxOutputs-1`; setting a slot past the candidate's
  current size grows it). Accepts `GRB`, `RGB`, `RBG`, `GBR`, `BRG`, `BGR`
  independently per output. The WHOLE candidate is validated (no duplicate
  pins, only output-capable GPIOs, excluding flash GPIO6..11 and console
  GPIO1/3; 1..2048 pixels per output; valid color order;
  1..`kMaxOutputs` slots) before the edit is kept -- an invalid edit leaves
  the staged candidate completely unchanged. Stages only, never the active
  topology.
- `AT+OUTDEL=<slot>` -- removes one slot from the candidate, shifting later
  slots down to stay dense (no holes). Rejected if it would leave zero
  slots. Stages only.
- `AT+OUTSAVE` -- validates the whole staged candidate and persists it.
  **Takes effect on the next explicit reboot only** -- it never live-replaces
  the running adapter/Composer topology. A candidate incompatible with the
  nonempty durable current scene is rejected; explicitly clear/publish that
  scene before removing/changing its output prefix. Subsequent scene saves
  are checked against the next-boot topology too: after saving an incompatible
  topology for an empty scene, reboot before saving a new scene. Otherwise
  the existing `applied; NOT saved` error is reported and old storage stays
  intact. A topology-save failure reports the error and leaves the previously
  saved configuration completely unchanged (the candidate stays staged for
  a retry).

Default boot remains GPIO16/50/GRB (unchanged since before this delivery); a
second output is only ever activated by an explicit `AT+OUTSET`+`AT+OUTSAVE`
followed by a reboot. Wiring/count for a real second strip is to be agreed
with Rodrigo during the physical bench setup, after this implementation.

### Global logical indices across outputs

Segments retain one GLOBAL logical index space across every configured
output, in slot order -- there is no second, per-output numbering
convention in `AT+SEGSET`. For output counts 50 and 20: output 0 is
`[0,50)`, output 1 is `[50,70)`; a segment may address either range
independently, or cross the boundary between them (e.g. `[45,55)`) -- every
segment is validated only against the topology's own combined
`totalPixels()` (70 here), never against either output's own count
individually. `examples/basic/pixels_scene_test`'s own `codecTests()`
demonstrates exactly this (independent segments on each output, plus one
crossing segment, plus rejection one past the combined total).

### Persistence/compatibility across topology changes

A record's own embedded topology (pins/counts/order, 1..`kMaxOutputs`
slots) must be an exact-match **prefix** of the device's current topology to
be recalled (`bench::isCompatiblePrefix()`, `main/output_config.hpp`): every
slot the record itself knows about must match the current device's own
same-index slot exactly; the current device may have MORE slots than the
record (an old single-output record recalled after a second output was
appended -- the original slot 0 keeps its exact meaning, the added slot is
simply not covered by any of that record's own segments, and renders black),
but never FEWER. This applies uniformly to the current-scene journal
(`SceneStore`) and every one of the 100 named presets (`PresetStore`) alike.

- Loading a preset never unexpectedly reassigns physical pins: an
  incompatible preset is rejected before anything changes, with the
  previously active scene/staged table left completely untouched, and the
  stored record itself is never modified either way -- it becomes
  recallable again the moment the topology it expects is restored.
- The preset catalog always selects the newest structurally valid A/B
  record, regardless of active topology. Incompatible presets remain listed
  and occupy their slots; loading reports an incompatible pin/count/order
  error. A new name cannot overwrite their slots or select an older version
  merely because that version happens to be compatible.
- Saving a preset (`AT+PRESETSAVE`) always uses the device's own **active**
  topology, never an unpublished/staged `AT+OUTSET` edit.
- All 100 existing named presets and the existing current-scene record
  remain readable exactly as before (schema 1, single-output, unchanged) --
  see `examples/basic/pixels_scene_test/main/test_scene.cpp`'s own
  compatibility gates (recall after an output is appended; rejection when
  the first output itself changes) and the "Automatic configuration
  retention" section above for the schema-1/schema-2 read/write split.
- No record is ever bulk-rewritten at boot as a migration, and NVS is never
  erased as part of this delivery.

### Recalculated maximum record size and partition fit

Schema 2's own variable-size header grows from schema 1's fixed 32 bytes to
31 bytes at one output up to 36 bytes at `kMaxOutputs`==2 (`main/
scene_codec.hpp`'s own `kSceneHeaderBytesV2Max`) -- a **4-byte** increase at
the worst case (two outputs) over the old single-output-only maximum.
Recalculated maximums, 32 segments (`comp::MAX_SEGMENTS`), `kMaxOutputs`==2:

| Record | Old max (schema 1 only) | New max (schema 2, 2 outputs) |
|---|---|---|
| Scene (`kSceneRecordBytes`) | 1124 bytes | **1128 bytes** |
| Preset (`kPresetRecordBytes`) | 1177 bytes | **1181 bytes** |

The existing 320 KiB `presets` partition (`partitions_wroom_presets.csv`)
still fits comfortably, **no partition layout change is needed**: both 1177
and 1181 bytes round up to the same `ceil(x/32) == 37` NVS data entries (the
4-byte growth falls inside the same 32-byte-entry bucket), so the existing
worst-case accounting of 41 entries/record, 8200 entries for all 100 A/B
preset positions, and 1880 entries of headroom out of the partition's 10080
usable entries, is unchanged -- see that file's own updated comment for the
full recalculation. `OutputStore`'s own record is tiny (<=31 bytes at
`kMaxOutputs`==2) and reuses the existing 24 KiB default `nvs` partition
(namespace `pixels_outputs`, keys `outputs_a`/`outputs_b`, the same A/B
journal discipline `SceneStore` already has) -- no new partition needed for
it either.

### Test coverage added

- `adapter_esp_pixels/test/test_adapter.cpp` (172 checks passed on-target
  through `examples/basic/pixels_adapter_test`, GPIO4/5 disconnected):
  two-real-output state machine, independent per-slot wire order,
  `addOutput()` duplicate-pixels/duplicate-pin/`kMaxOutputs`-exceeded
  rejection (checked in that priority order), a faulted single slot's own
  `show()` failure not blocking the other slot's real `commit()`/`wait()`
  drain, and `end()`'s own multi-slot partial-drain-failure/retry contract.
- `examples/basic/pixels_scene_test` (5480 checks passed on-target with
  RAM-mocked NVS -- see its own README): `test_scene.cpp`'s
  compatibility gates (append-then-recall, changed-first-output-rejected)
  and the global-index boundary-crossing demonstration; a new
  `test_outputs.cpp` (`OutputStore` journal: load/save/torn-write/
  failure-retry/invalid-candidate-rejection, mirroring `SceneStore`'s own
  gates; `outputsAt` AT-library integration: staged-only mutation with a
  whole-candidate validation, rejected edits leaving the candidate
  unaffected, `AT+OUTDEL` shifting slots down, and a failed `AT+OUTSAVE`
  preserving the previously durable configuration exactly). The existing
  5224-check baseline is extended, never shrunk, by all of the above.

### Bench sequence

Bench 2026-09-14: application and both suites flashed with verification at
115200 baud (higher-speed backup reads lost communication; no writes were
attempted at those speeds). Before testing, the complete 24 KiB `nvs` and
320 KiB `presets` partitions were copied and verified against flash.
GPIO16/50 and GPIO17/12 GRB were configured through `OUTSET`/`OUTSAVE` and
confirmed after reboot. The original Pos100 scene remained LED 34 red,
brightness 96, with the second output uncovered/black by the scene definition.

Real NVS accepted **201 maximum-size 1181-byte preset writes**: all 100
existing positions twice, then Pos100 once more, filling both journal
sides with schema-2/two-output/32-segment records. A new 101st name was
rejected with `ESP_ERR_NO_MEM`. After software reboot, all 100 test records
loaded successfully (305 command checks total). This kept the same marker
scene throughout; it does not prove visual boundary routing.
The original `presets` partition was then restored and verified against its
backup (SHA-256
`F1A33D330EB20A7D38B3AFAB4EEBF7856F16FE7E7F661B62A6DECDF68FBF398D`).
The restored application lists exactly Pos001..Pos100, with the two-output
configuration retained. Logs are in
`build-wroom-rmt-safe/bench-outputs-20260914/` (`capacity.log`,
`presets-restore-verify.log`, `restored.log`).

Rodrigo then confirmed the complete physical sequence: GPIO16's 50 pixels
red, GPIO17's 12 blue, followed by one green segment at global `[45,55)`
(last five pixels of output 0 and first five of output 1). Pos100 was used
temporarily for the three-segment scene. Disabling all three segments
produced an explicit blackout; preset recall and software reboot recovered
the same scene. The first script's empty publication retained the preceding
frame, as documented above, so that blackout step was repeated correctly;
no production change was needed.

The original preset partition was restored again and verified against the
same backup. All **100 legacy presets** then loaded on the two-output
topology, each marker's index, RGB color and Active state checked through AT
(203 command checks passed). Final reboot retained GPIO16/50 and GPIO17/12,
exactly Pos001..Pos100 in the catalog, and the original Pos100 scene:
physical LED 34 red, brightness 96, second output black. Captures:
`visual.log`, `visual-recall.log`, `presets-final-verify.log`,
`legacy-recall.log` and `final-state.log` in the same directory.
This delivery is checkpointed locally, without push or tag.

1. Agree the second strip's physical pin/pixel count with Rodrigo; wire it.
2. Build/flash `build-wroom-rmt-safe` with the new adapter/application stack;
   confirm the `PIXELS_AT` ready line reports `outputs=1 total=50` (the
   unchanged default) and `persistence=scene-v2`.
3. `AT+OUTSET=1,<pin>,<count>,GRB` then `AT+OUTSAVE` (expect the "next
   reboot" wording), then reboot; confirm `AT+OUT?` now reports two active
   outputs and the boot log's own `outputs=2 total=<50+count>`.
4. Stage two segments with different colors/effects, one per output (e.g.
   `AT+SEGSET=1,0,50,<effect>` and `AT+SEGSET=2,50,<50+count>,<effect>`),
   plus one segment crossing the boundary (e.g. `[45,55)`); `AT+SEGPUB`;
   visually confirm all three regions on the physical strips.
5. The store is already full: `TwoStrips` cannot be a 101st preset. Arrange
   a verified backup and a temporary test position before the named
   two-output save/load check; restore the original preset data afterward.
   Do not silently overwrite a user's preset to make room for a test.
6. `AT+RESET` with no further commands; confirm current two-output scene
   retention. After restoring the original preset data, `AT+PRESETS?`
   must report exactly `Pos001` through `Pos100`, each loadable on output 0
   with output 1 black. Preserve both the names and their original records.
7. Capture serial logs for each step, the same way every earlier bench run
   in this README has.

## 2D canvas / ledmap (PIXELS_MAP) — validated local checkpoint, unreleased

Claude implemented this brief (`docs/pixels-2d-task.md`) against app baseline
`5361eb090a9abb4640326074028d3d81c24c1cea` and Composer baseline
`07eb426a9c72f4b53062ba62d3a9a63370534755`. The hard part -- a real ledmap
and 2D segments -- already existed in `esp_pixels_composer` v0.2.0
(`Composer::setLedmap()`/`addSegment2D()`); this delivery adds the
application-level matrix configuration/persistence/compatibility around it,
plus the one missing AT command (`AT+SEGSET2D`, Composer v0.2.1) and the
first genuinely-2D effect (`XYGradient`, esp_pixels_fx v0.2.1). Codex completed
integration and hardware execution; Rodrigo confirmed the virtual 5x10
demonstration on the straight strip. See the test counts and visual bench
sequence at the end of this section. The local checkpoint was closed on
2026-09-21.

### Matrix configuration AT library (`PIXELS_MAP`)

A new, application-owned AT library (`main/map_config.hpp`, `main/
map_store.*`, `main/map_at.*`), entirely separate from `PIXELS_COMPOSER`/
`PIXELS_PRESETS`/`PIXELS_OUTPUTS`: discover it the same way, `AT+HELP?` then
`AT+PIXELS_MAP?`. Generates a real ledmap table from a compact descriptor
(width/height/first logical pixel/row wiring) at boot and installs it via
`Composer::setLedmap()`, once, right after `build()` -- never a per-preset
table.

- `AT+MAP?` -- lists the ACTIVE canvas (what `Composer::setLedmap()`
  actually installed), the durably SAVED-for-next-boot one, and the STAGED
  candidate, each as `identity` or `width=<w> height=<h> first=<f>
  wiring=<PROGRESSIVE|SERPENTINE> range=[<f>,<f+w*h>)`. Query only.
- `AT+MAPSET=<width>,<height>,<first>,<PROGRESSIVE|SERPENTINE>` -- stages
  one whole candidate matrix, validated (`width`/`height` > 0, `first +
  width*height <= totalPixels()`) against whatever output topology is
  CURRENTLY durably saved for the next boot (never an unsaved staged
  `AT+OUTSET` candidate) -- the topology that will actually be active the
  next time this map takes effect. An invalid candidate leaves the staged
  one completely unchanged. Stages only.
- `AT+MAPCLR` -- stages the identity map (no real matrix). Does **not**
  touch the current scene or storage.
- `AT+MAPSAVE` -- re-validates the whole staged candidate against the same
  saved-topology bound, rejects it if incompatible with the durable
  nonempty current scene, then persists it. **Takes effect on the next
  explicit reboot only** -- never live-replaces the running
  `Composer::setLedmap()` canvas (which cannot change after `build()`
  established it anyway).

Origin is top-left, X right, Y down, row-major. PROGRESSIVE runs every row
the same direction; SERPENTINE alternates per row (row 0 forward, row 1
backward, ...). `first` is the GLOBAL logical index of canvas cell `(0,0)`
-- not a per-output index, and a matrix may cross an output boundary the
same way a 1D segment already can. This increment is deliberately a single,
uniform rectangle for the whole canvas: no arbitrary map-entry editor, no
holes, no panel tiling/rotation, no separate canvas per output -- limits of
this first AT increment, not of `Composer::setLedmap()` itself.

### `AT+SEGSET2D` and the new `XYGradient` effect

`AT+SEGSET2D=<id>,<x>,<y>,<width>,<height>,<effect_id>` (Composer v0.2.1,
see its own README) stages a 2D segment the same way `AT+SEGSET` already
stages a 1D one, validated against the real canvas
(`canvasWidth()`/`canvasHeight()`). 1D and 2D segments compose in the same
layer order, on the same canvas -- freely mixed in one scene. Under a real
matrix, a 1D segment's `[start, stop)` addresses the flat row-major CANVAS
space (`y*width + x`), never the physical/logical space directly; only the
installed ledmap resolves canvas indices to physical pixels.

`esp_pixels_fx` v0.2.1 adds `XYGradient` (effect id 4), the first
registered effect whose own output genuinely depends on both coordinates
(`FX_DIM_2D` only) -- a scrolling palette gradient whose `intensity`
control tilts the X/Y weighting asymmetrically (`0` = pure horizontal,
`255` = pure vertical, anything between mixes both axes at deliberately
different rates). See its own `README.md` for the full contract.

### Persistence/compatibility

Scene/preset records evolve to schema 3 (`main/scene_codec.*`): a shape
discriminator plus each segment's own active geometry (2D `x`/`y` reuse the
1D `start`/`stop` bytes; `w`/`h` and the discriminator are the only new
per-segment fields -- never a redundant second geometry pair), and one
compact map descriptor per record (7 bytes: width/height/first/wiring) --
never a full generated ledmap table. Schema 1 and 2 (this application's own
earlier, map-less formats) are still read unchanged, always implying the
identity canvas.

Compatibility checks output topology and map together. Identity records with
only 1D segments may use IDENTITY or a zero-offset PROGRESSIVE matrix covering
their whole original flat domain, preserving every physical index. A smaller,
offset or serpentine matrix rejects those records before mutation. Records
with 2D segments require unchanged canvas/map semantics; a stored real matrix
requires the same width/height/first/wiring. Output-prefix compatibility is
also required. Returning to IDENTITY with the original output prefix makes
all 100 old presets usable again; incompatible presets remain listed/occupied.
The current-scene guard (`SceneStore::canRestoreOn()`/`nextBoot()`) now
takes topology and map TOGETHER: `AT+OUTSAVE` validates its own new
candidate against whatever map is CURRENTLY durably saved
(`MapStore::current()`), and `AT+MAPSAVE` validates its own new candidate
against whatever topology is CURRENTLY durably saved
(`OutputStore::current()`) -- covering both save orders, never the OTHER
command's own unsaved staged candidate. Later scene saves are checked
against the combined saved next-boot topology AND map together, exactly
like the multiple-outputs delivery's own topology-only guard, now extended.
No record is ever bulk-rewritten at boot as a migration, and NVS is never
erased as part of this delivery.

### Recalculated maximum record size and partition fit

Schema 3's own header grows by exactly 7 bytes over schema 2 (the map
descriptor, always present, identity or not), and each segment grows by 5
bytes (`kSceneSegmentBytesV3` = 39, was `kSceneSegmentBytes` = 34: a 1-byte
shape discriminator plus `w`/`h`; `x`/`y` reuse the existing `start`/`stop`
bytes, so there is no redundant second geometry pair on disk). Recalculated
maximums, 32 segments, `kMaxOutputs`==2:

| Record | Schema 2 max | Schema 3 max (this delivery) |
|---|---|---|
| Scene (`kSceneRecordBytes`) | 1128 bytes | **1295 bytes** |
| Preset (`kPresetRecordBytes`) | 1181 bytes | **1348 bytes** |

The existing 320 KiB `presets` partition still fits, but with a
**meaningfully thinner margin than the previous delivery reported** -- this
is reported transparently rather than assumed: `ceil(1348/32) == 43` NVS
data entries per record (was 37 at 1181 bytes -- crossing into a new
32-byte-entry bucket, unlike the previous delivery's own 4-byte growth,
which stayed inside the same bucket). Using the same "+4 entries for index/
chunk-header overhead, split across two pages" accounting the partition
CSV's own comment already establishes: 43+4 = 47 entries/record max, x100
positions x2 (A/B) = **9400 entries**, against 10080 entries in 80 pages.
The gross margin is 680 entries. Reserving one 126-entry GC page and one
namespace entry leaves 553; budgeting a further 47-entry replacement record
leaves 506. This is conservative sizing, not a substitute for the required
real-NVS run with 1348-byte records. The partition layout remains unchanged.
`MapStore`'s own record is tiny (27 bytes fixed) and reuses the existing 24
KiB default `nvs` partition (namespace `pixels_map`, keys `map_a`/`map_b`,
the same A/B journal discipline every other store here already has) -- no
new partition needed for it either.

### Test coverage added

- The permanent `pixels_scene_test` runner now includes XYGradient's actual
  tests and the Composer AT regression for SEGSET2D, mixed 1D/2D layers,
  output-boundary routing, invalid edit/load preservation, shape replacement,
  and height-one geometry. AT registration assertions now expect 22 commands.
- Map tests cover exact nonsquare progressive/serpentine tables and offset,
  invalid wiring/identity descriptors, both output/map save orders, unsaved
  candidate isolation, current-scene protection and 2D reboot recovery.
- Codec tests retain new-schema maximum-size checks and include independent
  schema-1/2 fixtures, progressive identity-domain compatibility, serpentine
  rejection and transactional malformed-record checks.

Review 2026-09-15: application, expanded scene suite and independent adapter
runner all built successfully. Application binary: 259648 bytes; scene-suite
binary: 544144 bytes. Suite static DRAM: 172488/180736 bytes (95.44%), leaving
8248 bytes. Added temporary test fixtures use checked heap allocations and are
released between cases. On WROOM/COM6 the expanded suite passed 6180 checks
and the independent adapter runner passed 172 (6352 total, zero failures).
The application was restored with the same partition layout and RMT cache
safety enabled. Build logs: `2d-review-build.log` in each build directory;
serial captures and verified partition backups:
`build-wroom-rmt-safe/bench-2d-20260915/`. Rodrigo confirmed the visual
sequence on 2026-09-16 and reaffirmed it on 2026-09-21.
The real-NVS capacity run also passed: 201 writes of maximum-size 1348-byte
presets (both A/B copies across 100 positions plus replacement at capacity),
rejection of a 101st name, reboot and 100 recalls. No partition growth was
needed. The original preset partition is restored from the verified backup
after this temporary capacity exercise.
### Visual bench sequence (executed and confirmed)

Rodrigo requested validation on the straight GPIO16 strip (50 pixels),
without assembling a panel. Use a virtual 5-column x 10-row matrix at global
offset zero: each consecutive group of five LEDs represents a row. GPIO17
(12 pixels) stays connected but unmapped/black. This demonstrates coordinate
and wiring behavior on real LEDs; it is not an assembled-panel observation.

1. Arrange observation of the straight GPIO16 strip with Rodrigo. Verify backups
   of the original NVS/preset partitions before changes. Flash the suites, run
   them, then restore the application. Boot must show `outputs=2 total=62`,
   identity `canvas=62x1`, and `persistence=scene-v3`.
2. Explicitly clear/publish the old current scene before changing its layout:
   `AT+SEGCLR`, `AT+SEGPUB`. Then `AT+MAPSET=5,10,0,PROGRESSIVE`,
   `AT+MAPSAVE`, `AT+RESET`. Confirm `canvas=5x10`.
   Clearing an empty scene holds the last physical frame; startup sends black.
3. Use Solid 2D rectangles to show row y=4 (physical LEDs 21..25) and column
   x=0 (LEDs 1,6,11,...,46). XYGradient (`effect=4`, speed zero) over 5x10
   shows repeated horizontal ramps at intensity zero and row bands at 255.
   After explicit clear/save/reboot into SERPENTINE, column x=0 must instead
   light LEDs 1,10,11,20,21,30,31,40,41,50. All physical positions here are
   numbered from one; AT coordinates start at zero.
4. The store has 100 occupied positions. Use a verified, temporarily borrowed
   position such as Pos100 for matrix save/recall; do not create a 101st name.
   Disable/publish the active segments for an explicit blackout, recall the
   test preset, and reboot to confirm 2D scene recovery.
5. Confirm an old 50-pixel identity preset is usable on the progressive 5x10
   canvas, but rejected on SERPENTINE without replacing staging/live state;
   all 100 names remain occupied.
   Run the 1348-byte real-NVS capacity/replacement gate separately, keeping
   backup/restore protection and the 100-position limit.
6. Explicitly clear/publish the matrix scene, then `AT+MAPCLR`, `AT+MAPSAVE`,
   `AT+RESET`. Restore and verify the original preset partition, recall all
   Pos001..Pos100 with geometry/color checks, and finish on Pos100.
7. Keep serial captures. Builds, automated execution and physical observation
   are recorded separately.

The progressive sequence passed all 34 serial checks, including an original
schema-1 preset recall on the 5x10 canvas, XYGradient controls, preset recovery
after explicit blackout, and restored 2D geometry/controls after reboot.
The serpentine sequence passed all 17 checks: old identity and progressive-map
presets were rejected without changing the blue-column scene, all 100 names
remained occupied, and the blue 2D column survived reboot. Captures:
`visual-progressive.log` and `visual-serpentine.log` in the bench directory.
After the demonstration, IDENTITY was restored and the original preset
partition was restored again and verified byte-for-byte against its backup.
All 100 original presets passed geometry/color recall checks (203 serial
checks, zero failures). Final scene: Pos100, LED 34 red, second output black,
brightness 96. Rodrigo confirmed the red row, green column, X/Y/mixed
gradients, blackout/preset/reboot recovery and serpentine blue column with
GPIO17 black. The 2D delivery is checkpointed locally after that confirmation;
no push or tag was performed. This validates a virtual canvas on a straight
strip, not an assembled matrix panel.
## Symmetric 1D mirror (PIXELS_COMPOSER `AT+SEGMIRROR`) — validated locally, unreleased

Claude implemented this brief (`docs/pixels-mirror-task.md`) against app
baseline `d6e5665845f0df580fd6e1a41680ea6690f830a3` and Composer baseline
`5dc3a8ebec0e85136d28c07914e6030d25b2c370`. Codex completed review and
hardware execution on WROOM/COM6. Rodrigo confirmed even50/odd49 Rainbow,
reverse interaction, mirrored Sinelon crossing GPIO16/17, explicit blackout,
named preset recall and reboot recovery. This remains a local, unreleased
delivery; see execution evidence and the application stack fix below.

A new, explicit segment-level `mirror` boolean (`Segment`/`SegmentSpec`,
default `false`, independent of `reversed`/effect params) makes an existing
1D destination span `[start, stop)` (width `N`) symmetric: the effect itself
sees a narrower SOURCE `Surface` of width `M = ceil(N/2)`; composition folds
each destination offset `d` onto source index `min(d, N-1-d)`, then applies
`reversed` to that already-folded index -- reverse acts AFTER the fold, not
before it. `mirror == false` reduces identically to the existing `reversed`
mapping (`M == N`, `source = d`). See `esp_pixels_composer`'s own README
(`Added in v0.2.2`) for the full composition/carry-forward contract, byte
layout, and `AT+SEGMIRROR=<id>,<0|1>` command. No new effect, no HAL change,
no new output/map subsystem, no 2D mirroring -- purely Composer's own
segment composition plus this application's schema-4 durable encoding.

### Persistence/compatibility

Scene/preset records evolve to schema 4 (`main/scene_codec.*`): schema 3's
own exact 39-byte-per-segment layout and 38-byte header are kept BYTE-
IDENTICAL -- only the meaning of ONE already-existing byte changes, from a
strict 0/1 `reversed` value to a 2-bit flags byte (bit0 `reversed`, bit1
`mirror`). An unknown bit (`flags > 3`) is rejected outright, never silently
ignored. Schema 1/2/3 records are read unchanged, through their OWN
dedicated, untouched decode functions, always implying `mirror = false` --
schema 3 in particular keeps its own strict 0/1 validation: a byte that is a
valid schema-4 mirror-only flag (`2`) is correctly REJECTED when the SAME
bytes are read back as schema 3 (proven directly in `test_scene.cpp`'s own
`codecTests()`, byte-for-byte, not by inference). `encodeScene()` never
produces schema 1/2/3 again, matching every prior schema bump's own
discipline. `mirror` is rejected outright for any 2D segment, including
`h == 1`, both at encode time (`valid()`) and independently at decode time
(a hand-crafted, otherwise-CRC-valid record with `is2D` and the mirror bit
both set is rejected before any output mutation). No record is bulk-
rewritten as a migration; NVS is never erased.

### Recalculated maximum record size and partition fit — unchanged

Schema 4 is deliberately size-NEUTRAL: same `kSceneSegmentBytesV3` (39),
same `kSceneHeaderBytesV3Max` (38, one output/identity map), same
`kSceneRecordBytes`/`kPresetRecordBytes`. Verified by direct arithmetic
(`kMaxOutputs==2`, `MAX_SEGMENTS==32`) AND by the existing maximum-size
round-trip tests (`test_scene.cpp`/`test_presets.cpp`), which assert the
exact byte count and would fail immediately if the layout had shifted:

| Record | Schema 3 max | Schema 4 max (this delivery) |
|---|---|---|
| Scene (`kSceneRecordBytes`) | 1295 bytes | **1295 bytes (unchanged)** |
| Preset (`kPresetRecordBytes`) | 1348 bytes | **1348 bytes (unchanged)** |

The existing 24 KiB `nvs` (map) and 320 KiB `presets` partitions are
therefore unaffected by this delivery -- the prior delivery's own NVS-entry
accounting (`ceil(1348/32)==43` data entries/record, 47 with index/chunk
overhead, 9400 entries at 100 positions x2 A/B, against 10080 in 80 pages)
still applies unchanged, since nothing about the on-disk size moved.
`preset_codec.*`'s own wrapper format (`kSchema=1`, unrelated to the inner
scene's own schema number) needed no code change at all -- it already
threads the inner scene's bytes through opaquely; only a stale doc comment
referencing schema-2-era sizing was corrected.

### Test coverage added

- `esp_pixels_composer`'s own `ci_harness` (`test/test_composer.cpp`): a new
  `FX_TEST_WIDTH` fixture asserting the exact SOURCE Surface width
  (`M = ceil(N/2)`); a byte-exact reproduction of the task's own N=1/2/5/6
  worked-example table (both `Mirror` and `Mirror + reverse` columns) plus
  mirror-off parity; odd-segment center-composed-exactly-once; a history-
  reading effect proving the retained buffer stays SOURCE-sized and
  survives across calls; a nonzero-start segment crossing two `FakeSink`
  outputs through a real, hole-containing ledmap; mirrored-2D rejection at
  both `h==1` and genuinely 2D, with existing 2D coverage confirmed
  unaffected; exactly-one-restart on a mirror-only toggle (including at
  `N==2`); `addSegment()`'s new trailing `mirror` parameter's backward
  compatibility; and a mirrored `applyConfig()` allocation failure leaving a
  live scene untouched.
- `test/test_composer_at.cpp`: `AT+SEGMIRROR` stage/query(via
  `exportConfirmedScene()`)/publish, every documented invalid-input
  rejection, rejection when staged 2D, `AT+SEGSET` preserving it on
  re-stage, `AT+SEGSET2D` clearing it (and `reversed`) on shape conversion,
  a real `AT+SEGPUB` round trip proving the actual fold through the AT path,
  and `attachAtPersistence()` restoring it on attach. Registration count
  updated 22 -> 23 everywhere it is asserted.
- `pixels_scene_test`'s own `test_scene.cpp`/`test_presets.cpp`: the
  existing 32-segment maximum-size round trips (scene AND preset) now vary
  `mirror` across all four flag combinations and assert it survives byte-
  exact; the schema-4 magic/schema-field bytes are checked directly; a
  dedicated block re-reads the SAME schema-4 bytes as schema 3 to prove
  strict 0/1 rejection of a mirror-only flags byte, then strips the mirror
  bit to prove schema 3 still decodes correctly on its own terms; a raw
  mirrored-2D record is rejected on decode before mutation; and
  `legacySchemaTests()`'s existing schema-1/2 fixtures now explicitly assert
  `mirror == false`. No existing test was removed, weakened, or had its
  count assertion lowered.

Static DRAM in `pixels_scene_test`: the prior delivery reported 8248 bytes
free (172488/180736 used); this delivery's substantial new registered
coverage (across all four files above, all linked into this one image)
brings that to **2224 bytes free (178512/180736 used, 98.77%)** -- a real,
build-verified, still-positive margin, not a leak: one avoidable ~3 KB
static `AtScene` pair was converted to heap (`std::nothrow`, matching this
suite's own established pattern) once discovered during the build, and no
coverage was dropped to fit. Final binary sizes: application 261280 bytes;
`pixels_scene_test` 575792 bytes after the fixes below; independent
adapter 187088 bytes. The expanded scene runner executed 6588 checks and the
adapter 172 on WROOM/COM6: **6760 checks, zero failures**, retaining the
6352-check baseline. Runtime allocation gates also passed.

The first execution exposed six failed assertions in two test setups, fixed
without changing Mirror production code: accepted 2D configurations needed a
render/adoption before checking the live count, and the 10000-pixel allocation
failure fixture needed a sink accepting the whole submission while retaining
only its first eight pixels. Four adoption/reclaim assertions were added.
The application boot banner was corrected to report scene-v4.
Logs/backups: `build-wroom-rmt-safe/bench-mirror-20260921/`.

A real stack overflow was then caught during legacy preset recall. Although
the handlers' scratch scenes were static, assigning `AtScene{}` created a
large temporary on the 4096-byte AT stack. The redundant whole-scene resets
were removed from preset save/load: export/decode already fill every used
segment and all scene globals, and failure never consumes the scratch result.
Compiled handler stack frames are now 48 bytes. Core stack size and Mirror
production code remain unchanged. The expanded suite passed again afterward.
All 100 original presets then passed position/color recall (203 serial
checks). A maximum-size 32-segment preset also passed real schema-4 save,
recall and reboot. The original 100-preset partition was restored again and
verified against its backup. Final state: IDENTITY, Pos100, LED34 red,
brightness96, GPIO17 black. Rodrigo confirmed the Mirror visual sequence.

### Bench sequence (executed and visually confirmed)

Uses the existing Rainbow/Sinelon effects and the current two-output
topology (GPIO16/50, GPIO17/12) -- no new effect needed. **Use a BACKED-UP
EXISTING position (e.g. Pos100) for any temporary staging, never a 101st
name; restore all 100 originals and finish on Pos100**, exactly like the
prior two deliveries.

1. Verify backups of the current NVS/preset partitions before changes. Flash
   the application and the expanded `pixels_scene_test`; run both; restore
   the application afterward. Run the independent adapter suite too; the
   combined suite counts must retain at least the 6352 baseline checks,
   with zero failures.
2. `AT+SEGCLR`, `AT+SEGSET=1,0,50,2` (Rainbow, `effect_id=2`, GPIO16's own
   50-pixel span), `AT+SEGPUB` -- baseline, mirror off. Confirm it fills the
   strip normally.
3. `AT+SEGSET=1,0,50,2`, `AT+SEGMIRROR=1,1`, `AT+SEGPUB` -- even N=50 span,
   `M=25`: the first and last 25 physical LEDs must show the SAME symmetric
   Rainbow pattern folded about the midpoint (LED 25/26), not the plain
   50-wide ramp. This is the "even50" demonstration.
4. `AT+SEGSET=1,0,49,2`, `AT+SEGMIRROR=1,1`, `AT+SEGPUB` -- odd N=49 span,
   `M=25`: LED 25 (the exact center) must show a single unrepeated color,
   with symmetric pairs on either side -- the "odd49" demonstration. Toggle
   `AT+SEGMIRROR=1,0` and republish to confirm the segment falls back to the
   plain (non-symmetric) 49-wide ramp, byte-different from step 4's own
   frame.
5. Reverse interaction: `AT+SEGSET=1,0,49,2,1` (start=0, stop=49, Rainbow
   `effect_id=2`, `reversed=1` as `AT+SEGSET`'s own trailing positional
   argument), `AT+SEGMIRROR=1,1`, `AT+SEGPUB`. The visible pattern must
   still be symmetric about LED 25 but with the fold-then-reverse order the
   task doc specifies (verified byte-exact in `test_composer.cpp`'s own
   worked-example gate) -- distinguishable from step 4's own (non-reversed)
   frame wherever Rainbow's own per-index color actually varies.
6. A segment crossing both physical outputs: `AT+SEGSET=2,40,60,1`
   (Sinelon, `effect_id=1`, spanning GPIO16's own tail and GPIO17's own head
   at the identity boundary index 50), `AT+SEGMIRROR=2,1`, `AT+SEGPUB`.
   Confirm Sinelon's own moving dot still animates correctly and
   symmetrically across the boundary over a few frames (proving the fold
   happens in canvas space, before ledmap/output routing, and that
   `FakeSink`-proven history-buffer isolation holds on real hardware too).
7. Preset recall: save the current mirrored scene to the BACKED-UP Pos100,
   explicitly disable/publish its segments to black out the scene, then
   `AT+PRESETLOAD=Pos100` to prove named recall. Separately `AT+RESET` and
   verify recovery of the saved current scene with mirror intact. Animation
   time restarts; compare geometry, symmetry and settings rather than phase.
8. Restore all 100 original presets from backup, recall Pos001..Pos100 with
   geometry/color checks, and finish on Pos100 -- IDENTITY map, LED34 red,
   brightness 96, GPIO17 black, exactly as the task's own baseline
   describes. Keep serial captures throughout; builds and physical
   observation are recorded separately.

## Custom effect parameters via AT (`AT+SEGCUSTOM1/2/3`)

### Codex hardware review — 2026-09-22

The expanded scene runner passed **7088 checks**, and the independent adapter
runner passed **172**: **7260 checks, zero failures**, on WROOM/COM6. Build
sizes: application 262272 B, scene runner 592352 B, adapter 187312 B. The
scene runner retains 2208 B of static DRAM headroom (98.78% used).

The first hardware run exposed invalid new codec fixtures: cycling through
IDs 0..5 selected 2D-only XYGradient for 1D segments. The codec correctly
rejected them; the test then computed a CRC over an empty record and panicked.
Both maximum-size fixtures now use the five 1D-capable IDs, including Stripes,
and stop after an initial encode failure. Production code was unchanged.
The Stripes boundary gate also now renders with all three custom bytes at
255, checking the transition from the final primary cell into the secondary
band. All previous gates remain registered.

Logs and fresh verified backups:
`build-wroom-rmt-safe/bench-custom-20260922/`. The successful scene capture
contains repeated `checks=7088 RESULT=ALL_PASS`; the capture tool initially
reported a missing expectation because its regex omitted the colon after
`SCENE_TEST`. The stored log was checked with the corrected pattern.

Serial bench passed 22 control/Mirror/reverse checks and 18 crossing/recall/
reboot checks, including the actual labels and all three custom values after
recovery. Fresh NVS/preset backups matched again after the suites. After the
visual sequence, both original partitions were restored and digest-verified;
all 100 original presets passed recall/geometry/color checks (203 commands).
Seven final queries confirmed IDENTITY, GPIO16/50 plus GPIO17/12, Pos100,
LED34 red, brightness96 and running. Rodrigo confirmed the complete visual
sequence after a slower replay in ten announced 20-second stages, including
green bands crossing the two outputs. Both original partitions were restored
and digest-verified again after that replay; seven final queries passed and
Rodrigo confirmed the final LED34-red state. Replay captures: `replay/` under
the same bench directory.

### Implementation handoff (before hardware execution)

Claude implemented this brief (`docs/pixels-custom-params-task.md`) against
app baseline `b6d78042d5992d157811437e3822a6ba02fe84c1` and Composer baseline
`c2c3e542ce00b60c9ae4c7d224244676bb8a2de1`, both unchanged (all edits below
are uncommitted working-tree changes, left for Codex's own review/bench/
checkpoint). **Compiled and verified via five clean builds (application,
Composer's own `ci_harness`, `esp_pixels_fx`'s own `ci_harness`, the expanded
`pixels_scene_test`, and the independent `pixels_adapter_test` runner); none
of this has been executed on real hardware yet.**

`FxParams::custom[3]`/`FX_USES_CUSTOM1/2/3`/`FxInfo::custom_labels[3]`
already existed in `esp_pixels_fx` before this delivery, but no effect
consumed them and no AT command exposed them. This delivery adds both: three
new Composer sidecar setters (`AT+SEGCUSTOM1/2/3`) and `Stripes`
(`esp_pixels_fx` v0.2.2, effect id 5), a static two-color 1D band pattern
that is the first shipped effect to actually declare the CUSTOM flags. See
`esp_pixels_composer`'s own README (`Added in v0.2.3`) for the full AT
contract and `esp_pixels_fx`'s own README (`v0.2.2`) for the effect's exact
formula. No HAL/transport or Composer rendering-engine change; the only
application-level change is test integration (the persistence layer already
carried all three custom bytes through every scene schema before this task
-- verified against the actual code, not assumed, see below).

### Persistence — no code change needed

`main/scene_codec.cpp`'s `encodeSegment()`/`decodeSegment()`/
`decodeSegmentV4()`/the schema-1/2 reader already loop over
`s.params.custom` unconditionally (`for (uint8_t custom : s.params.custom)
{ put(p, custom, 1); }` and its decode-side mirror) -- these three bytes
have been part of every segment record since before schema versioning even
began, entirely independent of which effect id a segment names. Adding
effect id 5 needed no schema bump: the byte layout and existing effect
meanings are unchanged. The codec DOES validate IDs through `fx::describe()`
and checks effect dimensions/work requirements in `valid()` before accepting
a segment. Appending the registered Stripes ID satisfies those checks.
`kFxCatalog` tracks incompatible effect IDs/parameter meanings, not only
record layout; this compatible addition does not require a bump. Schema
stays at 4; scene/preset maximum sizes are unchanged (verified below, not
just asserted). `preset_codec.*` needed no change either (unchanged since
the mirror delivery -- it already threads the inner scene's bytes through
opaquely).

### Recalculated maximum record size and partition fit — unchanged

No byte layout changed, so `kSceneRecordBytes`/`kPresetRecordBytes` are
identical to the mirror delivery's own already-verified values, reconfirmed
here by the same two independent methods (direct arithmetic, `kMaxOutputs
==2`/`MAX_SEGMENTS==32`; and the existing maximum-size round-trip tests,
which now also vary `effect_id` across the five 1D-capable IDs (including
`FX_STRIPES`) and, in `test_presets.cpp`, `custom[0..2]` per segment,
without changing `record.size`):

| Record | Prior (schema 4) | This delivery |
|---|---|---|
| Scene (`kSceneRecordBytes`) | 1295 bytes | **1295 bytes (unchanged)** |
| Preset (`kPresetRecordBytes`) | 1348 bytes | **1348 bytes (unchanged)** |

The existing 24 KiB `nvs`/320 KiB `presets` partitions are therefore
unaffected; the prior delivery's own NVS-entry accounting still applies
unchanged. No record is bulk-rewritten as a migration; NVS is never erased.

### The Mirror stack fix — preserved, not touched

`main/presets_at.cpp`'s own handler scratch scenes (`static
esp_pixels_composer::AtScene scene;`, no `= AtScene{}` reset temporary) were
verified intact and were not modified by this delivery at all -- this task
needed no change to preset save/load handling, so the file was never opened
for editing. Compiled handler stack frames remain the fixed 48 bytes Codex's
own bench run established; core stack size is unchanged.

### Test coverage added

- `esp_pixels_fx`'s own `test/test_stripes.cpp` (new): metadata (flags/dim/
  labels, palette/speed/intensity/bg genuinely absent); the task brief's own
  worked example (`custom=1,2,0` -> "two primary pixels followed by three
  secondary, repeating"), byte-exact; phase shifting the pattern by exactly
  one source cell (verified against the SAME unshifted sequence, not just
  "looks different"); boundary custom values `0` and `255` on both slots at
  once (alternating every cell / all-primary); source width 1; a width not
  a multiple of the period; and a combined complete-redraw/time-independence
  gate (pre-seeded marker pixels, a render at `now_ms=0` then a very
  different `now_ms`, byte-identical either way).
- `esp_pixels_composer`'s own `ci_harness` (`test/test_composer.cpp`): two
  new gates using the REAL `Stripes` effect (not a fictitious fixture) --
  one folding a deliberately non-palindromic band/gap pattern through
  `mirror`, both with and without `reversed`, byte-exact against the fold
  formula the Mirror delivery's own worked-example gate already proves; the
  other crossing two `FakeSink` outputs with an alternating pattern,
  confirming custom-driven routing and the pre-existing output-crossing
  contract hold together.
- `test/test_composer_at.cpp`: three new fictitious fixtures (`AT_TEST_
  CUSTOM`, full-featured; `AT_TEST_CUSTOM_PARTIAL`, custom-only, missing
  slot 2 -- doubling as the "absent flag" AND "custom-only effect" fixtures
  the task requires; `AT_TEST_CUSTOM_2D`, 2D-capable). Gates cover: new-
  segment defaults (`{128,128,128}`); staged-only (never touches `live_`
  before publish); every documented parsing rejection, looped over all
  three commands; the absent-flag rejection alongside the other two slots
  still accepted, plus a custom-only effect's own `AT+SEG=<id>?` still
  dispatching cleanly; shape-neutral acceptance on a staged 2D entry
  (unlike `AT+SEGMIRROR`'s own 1D-only gate); `AT+SEGSET` replacing a
  staged effect preserving all three custom bytes even when the NEW effect
  declares none of them (an unsupported slot stays DATA, never erased); a
  real `AT+SEGPUB` round trip covering all three slots separately including
  both boundaries (`0`/`255`); and `attachAtPersistence()` restoring custom
  bytes into both the live effect and `exportConfirmedScene()`. Registration
  count updated 23 -> 26 everywhere it is asserted.
- `pixels_scene_test`'s own `test_scene.cpp`/`test_presets.cpp`: the
  existing 32-segment maximum-size round trips now also cycle `effect_id`
  through the five 1D-capable IDs (including Stripes) and, in `test_presets.cpp`,
  vary `custom[0..2]` per segment with an explicit per-segment survival
  check after the full preset round trip -- `record.size` staying exactly
  `kPresetRecordBytes` throughout confirms the 1348-byte maximum really did
  stay unchanged, not just by assertion in this README.

Static DRAM in `pixels_scene_test`: the Mirror delivery's own bench-verified
baseline was 178512/180736 bytes used (2224 free); this delivery's own
build reports **178528/180736 bytes used (2208 free, 98.78%)** -- a real,
build-verified, still-positive margin, changed by only 16 bytes despite
substantial new registered coverage across four files, because every new
large scratch object (`AtScene` pairs in the new AT persistence/fixture
gates) was heap-allocated (`std::nothrow`, matching this suite's own
established pattern) from the start rather than added as a new `static`.
No coverage was dropped to fit. Binary sizes (this delivery, compiled, not
yet executed): application 262272 bytes; `pixels_scene_test` 592272 bytes;
Composer `ci_harness` 451360 bytes; `esp_pixels_fx` `ci_harness` 141776
bytes; `pixels_adapter_test` 187312 bytes.

### Proposed bench sequence (not yet run)

Uses the existing two-output topology (GPIO16/50, GPIO17/12) plus the new
`Stripes` effect (id 5) as the concrete demonstration -- no other new
effect. **Use a BACKED-UP EXISTING position (e.g. Pos100) for any temporary
staging, never a 101st name; restore all 100 originals and finish on
Pos100**, exactly like the prior three deliveries.

1. Verify backups of the current NVS/preset partitions before changes.
   Flash the application and the expanded `pixels_scene_test`; run both;
   restore the application afterward. Run the independent adapter suite
   too; the combined suite counts must retain at least the 6760 baseline
   checks, with zero failures.
2. `AT+SEGCLR`, `AT+SEGSET=1,0,20,5` (Stripes, `effect_id=5`, GPIO16's own
   first 20 pixels), `AT+SEGCOL=1,255,0,0`, `AT+SEGCOL2=1,0,0,255`,
   `AT+SEGCUSTOM1=1,1`, `AT+SEGCUSTOM2=1,2`, `AT+SEGCUSTOM3=1,0`,
   `AT+SEGPUB` -- the task brief's own worked example: two red pixels then
   three blue, repeating (band=2, gap=3, phase=0).
3. `AT+SEGCUSTOM3=1,1`, `AT+SEGPUB` -- confirm the whole pattern visibly
   shifts by exactly one pixel, nothing else changing.
4. `AT+SEGCUSTOM1=1,0`, `AT+SEGPUB` -- band shrinks to 1 (single red pixels
   between three-wide blue gaps); confirm each custom control acts
   independently of the others (gap/phase unaffected).
5. Mirror interaction: `AT+SEGSET=1,0,20,5`, `AT+SEGCUSTOM1=1,1`,
   `AT+SEGCUSTOM2=1,2`, `AT+SEGCUSTOM3=1,0`, `AT+SEGMIRROR=1,1`,
   `AT+SEGPUB` -- confirm the band/gap pattern now folds symmetrically
   about the segment's own midpoint (N=20, M=10), same fold contract the
   Mirror delivery's own bench sequence already demonstrated with Rainbow.
6. A segment crossing both physical outputs: `AT+SEGSET=2,40,60,5`
   (Stripes spanning GPIO16's own tail and GPIO17's own head at the
   identity boundary index 50), `AT+SEGCOL=2,0,255,0`, `AT+SEGCOL2=2,0,0,0`,
   `AT+SEGCUSTOM1=2,2`, `AT+SEGCUSTOM2=2,2`, `AT+SEGCUSTOM3=2,0`,
   `AT+SEGPUB` -- confirm the green/black band pattern continues correctly
   across the output boundary with no visible seam or reset.
7. Save the active Stripes scene to the BACKED-UP Pos100 FIRST, then
   disable/publish to black and use `AT+PRESETLOAD=Pos100` to
   confirm recall including all three custom values; separately `AT+RESET`
   and verify the saved current scene recovers identically (compare
   band/gap/phase, not animation phase -- Stripes has none).
8. Restore all 100 original presets from backup, recall Pos001..Pos100 with
   geometry/color checks, and finish on Pos100 -- IDENTITY map, LED34 red,
   brightness 96, GPIO17 black, exactly as the task's own baseline
   describes. Keep serial captures throughout; builds and physical
   observation are recorded separately.

## Known gaps

Review on 2026-09-14 fixed incompatible presets being cataloged as free,
output-save rejection/pending-topology checks for current-scene retention,
pin/count/order validation, all six RGB orders, truncated-record checks and
validation against the stored scene's own bounds. Added regression gates
and a separate `examples/basic/pixels_adapter_test` runner on unconnected
GPIO4/5. All 5652 automated checks passed on the WROOM/COM6. Two incorrect
test expectations found during execution were fixed: brightness 255 is
valid, and an empty preset includes both its inner and outer CRC32.
The application was restored with its unchanged partition layout and
RMT ISR cache safety enabled. GPIO16/50 and GPIO17/12 survived software
reboot with the original current scene and 100 preset names still present.
Logs/backups: `build-wroom-rmt-safe/bench-outputs-20260914/`.
Rodrigo confirmed physical boundary routing and preset/reboot recovery.

The agreed scope and proposed delivery order are tracked in the
[EasyLED / pixels roadmap](../docs/pixels-roadmap.md), including named
presets, 2D, multiple outputs, symmetric mirror, custom parameters and new
effects.

- Named presets are delivered locally (unreleased, automated hardware gates
  and real-NVS capacity passed; startup flash accepted as a known issue).
  Multiple physical outputs are delivered locally too (unreleased,
  hardware gates passed and visually confirmed). 2D canvas/ledmap is now
  delivered locally as well (unreleased, hardware checks passed and the
  virtual 5x10 demonstration visually confirmed -- see the section above).
- Symmetric 1D mirror passed hardware execution and visual confirmation.
  It is distinct from the existing `reversed` flag; see the mirror section
  above. Reflection of 2D segments is outside this delivery.
- Custom parameters now have three per-segment AT setters and Stripes as a
  production consumer; see this delivery's validation record above.
