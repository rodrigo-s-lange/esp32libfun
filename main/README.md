# WROOM-32 pixels AT bench

The root application drives 50 WS2812 pixels in GRB order on GPIO16.
At application startup it initializes the sink and sends a completed black
frame before reading NVS. The persistence bridge renders the recovered
scene once during attachment; the managed task later continues rendering.
This also clears the strip when booting with
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

## Automatic configuration retention (scene schema 1)

- A confirmed `AT+SEGPUB` saves the entire ordered scene. `AT+COMPAUSE=0|1`
  saves global pause with the last confirmed segments, never intervening staged
  edits. Queries and segment setters never write NVS.
- Retained: IDs, start/stop, effects, reverse, segment state, brightness, blend,
  opacity, speed, intensity, palette, color source, all three RGB colors, all
  three custom parameters, restart tokens, global brightness and global pause.
- Fixed GPIO16/50 pixels/GRB/RGB/identity mapping is checked at load. Arbitrary
  output topology, ledmaps and 2D scene persistence are **not** provided by this
  first application schema.
- Not retained: animation time, effect runtime, pixel/history buffers or task
  handles. Effects restart on boot. Paused segments/global pause restore as
  paused with initially black buffers until resumed; this is configuration
  restoration, not framebuffer restoration.
- Records use explicit little-endian fields, schema/catalog versions, a 64-bit
  generation and CRC32, not raw C++ struct bytes. Maximum record: 1124 bytes for
  32 segments; two segments: 104 bytes.
- Namespace `pixels_scene`, keys `scene_a`/`scene_b` in the existing NVS
  partition. Save targets the inactive key, commits and reads back; it does not
  overwrite the last valid key. Boot selects the newest valid compatible record;
  a torn/corrupt latest record can fall back to the preceding one. Redundancy
  within one partition does not protect against loss of both records or total
  flash failure. No automatic NVS erase is used.
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
| Composer and its adapter | `../esp_pixels_composer` | `0730626d2842a55d980b0848e636b579ecc81c62` on `feature/at-scene-persistence`: persistence, AT composition controls and confirmed-scene export/load; unreleased over published `v0.2.0` |
| FX | `../esp_pixels_fx` | local integration `e7e0ba391473005137f6fd4dcf70b4355975b6ac`; not yet published |
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

## Known gaps

The agreed scope and proposed delivery order are tracked in the
[EasyLED / pixels roadmap](../docs/pixels-roadmap.md), including named
presets, 2D, multiple outputs, symmetric mirror, custom parameters and new
effects.

- Named presets are delivered locally (unreleased, automated hardware gates
  and real-NVS capacity passed; startup flash accepted as a known issue).
  2D and multiple outputs remain pending; this bench
  still uses the fixed single-output, 1D scene schema described above.
- No symmetric (palindromic) mirror effect exists. The existing per-segment
  `reversed` flag only reverses which physical index a segment's local
  position `i` maps to; it is a local index flip, not the requested
  symmetric-mirror effect, and must not be presented as implementing it.
- No AT setters exist for the three per-segment custom effect parameters;
  they are retained by persistence only when already present in a
  programmatically-staged scene.
