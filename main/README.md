# WROOM-32 pixels AT bench

The root application drives 50 WS2812 pixels in GRB order on GPIO16.
`Composer::start()` owns rendering; `app_main()` returns after setup. The
adapter and Composer have static lifetime. Boot restores the last saved scene
from NVS before starting the managed task and AT reader. With no valid saved
scene it starts empty. Storage belongs to this application; the Composer
sidecar only has an optional, storage-neutral scene callback.

Only the `PIXELS_COMPOSER` sidecar is enabled. Discover it with `AT+HELP?`,
then `AT+PIXELS_COMPOSER?`. `AT+SEG?` inspects the staged table and
`AT+COMPAUSE?` reports managed-runtime state. Changes made with segment
setters take effect only after `AT+SEGPUB`.

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
  Flash access can still briefly stall rendering on ESP32.
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
| Composer and its adapter | `../esp_pixels_composer` | unreleased persistence bridge and AT composition controls on `feature/at-scene-persistence`, `97159d21ac5a6f4e1c1a71cc4792baade552b6df`, over published `v0.2.0` |
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
idf.py -B build-wroom-v020 -DIDF_TARGET=esp32 build
idf.py -B build-wroom-v020 -p COM6 flash
```

Close other serial monitors before flashing. Console baud rate is 115200.
The separate build directory preserves the preceding bench firmware artifacts.
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

## Known gaps

The agreed scope and proposed delivery order are tracked in the
[EasyLED / pixels roadmap](../docs/pixels-roadmap.md), including named
presets, 2D, multiple outputs, symmetric mirror, custom parameters and new
effects. The next implementation brief is [named presets](../docs/pixels-presets-task.md).

- Named presets, 2D and multiple outputs are required for the EasyLED MVP.
  Their AT/storage integration remains pending; this bench still uses the
  fixed single-output, 1D scene schema described above.
- No symmetric (palindromic) mirror effect exists. The existing per-segment
  `reversed` flag only reverses which physical index a segment's local
  position `i` maps to; it is a local index flip, not the requested
  symmetric-mirror effect, and must not be presented as implementing it.
- No AT setters exist for the three per-segment custom effect parameters;
  they are retained by persistence only when already present in a
  programmatically-staged scene.
