# Scene retention gates (ESP32)

Build with `idf.py -DIDF_TARGET=esp32 build`, flash explicitly with
`idf.py -p COM6 flash`, and capture 115200-baud serial. This image replaces the
bench application temporarily. It runs Composer core/AT suites plus the real
application `scene_codec.cpp`/`scene_store.cpp` and (unreleased)
`preset_codec.cpp`/`preset_store.cpp`/`presets_at.cpp` implementations.

NVS entry points are linker-wrapped in this binary only, shared by both
stores through `main/nvs_mock.hpp`/`.cpp` (one `__wrap_nvs_*` definition
per symbol, link-wide): `nvs_flash_init`/`nvs_open` back the current-scene
store's own `scene_a`/`scene_b` keys; `nvs_flash_init_partition`/
`nvs_open_from_partition` back the preset store's named `presets` partition;
`nvs_get_blob`/`nvs_set_blob`/`nvs_commit`/`nvs_erase_key` are shared,
dispatching by key shape. Faults cover missing records, deduplicated writes,
newest-record selection, corrupt newest-record fallback, torn inactive-slot
write, commit/readback failures, retry, empty-scene save and initialization
failure. No NVS erase or real scene/preset write occurs. Tests do not
simulate ESP-IDF's internal page allocator or prove physical power-loss
tolerance; actual NVS + reboot testing belongs to the root application.

Codec gates cover all meaningful fields with 32 segments, re-encoding equality,
explicit byte order, damage at every byte, length errors, CRC-valid invalid
schema/hardware/catalog/count/enums/effect/palette/source, duplicate IDs and
transactional output preservation. Substantial buffers have static lifetime.

Composer's AT gates verify restored live output/staging, no save on boot,
staging, query or cancel, exclusion of pending edits during pause save,
confirmed publish, applied-but-unsaved failure/retry, detach and validation,
plus (unreleased, added after the hardware run below) `AT+SEGBRIGHT`/
`AT+SEGBLEND`/`AT+SEGOPACITY`/`AT+COMBRIGHT?`/`AT+COMBRIGHT`: invalid input,
staged/live separation, an overlapping composition reaching `FakeSink`
byte-exact through every `BlendMode`, `FxRuntime` carry-forward across a
field-only republish, immediate global brightness (including while paused),
and their integration with the persistence gates above. All existing gates
remain registered.

`SCENE_TEST ... RESULT=ALL_PASS` repeats the results of one completed run;
suites do not rerun every two seconds. No physical pixel adapter is linked.

## Hardware result — 2026-09-08

WROOM-32/COM6, ESP-IDF 6.0.2, 8192-byte main stack and 48 AT slots:
1605 Composer checks + 817 AT checks + 1177 codec/journal checks = **3599**, all
passing. Serial capture: `build/hardware-validation.log` (generated artifact).
The first build found a missing test-only `<initializer_list>` include; fixed
before flashing. No production compiler warnings were introduced. Full configure
also reported existing ESP-IDF Git-submodule metadata/Kconfig notices and the
known unused serial-backend variable warning.

### Composition-control gates — hardware run 2026-09-09

The expanded suite ran on WROOM-32/COM6 with ESP-IDF 6.0.2:
1605 Composer + 1003 AT + 1177 codec/journal checks = **3785**, zero failures.
Capture: `build/composition-controls-hardware-validation.log`.

The first run found one incorrect test expectation: brightness 200 followed
by opacity 128 quantizes consecutive runtime markers 1 and 2 to the same
output byte. The test now checks the exact three-frame sequence 0,0,1,
which also distinguishes preserved runtime from a reset. Production code
was unchanged. Failure capture: `build/composition-controls-failure.log`.

These gates use FakeSink and wrapped NVS. They validate the new 21-command
sidecar and persistence integration, not the physical appearance of effects
or a new real-NVS recovery cycle for the composition controls.

### Named-preset gates — hardware run 2026-09-10

`test_presets.cpp` covers, in order: `preset_codec.*` directly (a
maximum-size 32-segment round trip, exhaustive per-byte corruption on a
1177-byte record, name-length/padding/generation-cross-check corruption,
`sequence == 0`, an empty-scene minimum-size record, and `validPresetName()`
directly) with no store or NVS involved at all; `PresetStore` through the
mock (two named presets, replace, delete of a known and an unknown name,
invalid names, corrupt-record recovery at boot via a second store instance
simulating reboot plus a runtime corruption caught by `load()` itself,
write/commit/readback failure each preserving the prior valid record with a
successful retry after); and the `PIXELS_PRESETS` AT library wired to a
real `Composer`/`FakeSink` (a confirmed `AT+SEGPUB` then a pending,
unpublished edit, proving `AT+PRESETSAVE` excludes it; `AT+PRESETLOAD`
applying segments/brightness/pause and triggering the current-scene save
path; `AT+PRESETLOAD` of a missing and of a corrupted name each leaving the
live configuration byte-exact on `FakeSink`; `AT+PRESETS?`/`AT+PRESETDEL`).
The capacity gate (fill every remaining position to exactly 100, reject a
101st distinct name with no data loss, replace an existing name while full)
runs last and reuses the same shared `PresetStore` instance every gate
before it already populated, rather than each gate paying for its own
~7 KB instance -- this image's own static RAM does not have room for more
than two simultaneous instances (the corrupt-recovery gate's own second,
reboot-simulating one being the sole exception).

Pre-flash review added output-preservation checks for a valid-CRC record
whose outer and inner generations disagree. Composer gates also publish a
pending staged edit after geometry/effect rejection to prove it survived,
and verify export observes current global brightness/pause. Runtime-load
pause checks account for an adoption frame rendered before global pause.

Executed on WROOM-32/COM6 with ESP-IDF 6.0.2: **5224 checks, zero failures**
(1605 Composer, 1053 AT, 1177 codec/journal, 1389 presets).
Capture: `build/presets-hardware-validation.log`. The first run had one
test-only failure: a read-fault injection was still enabled while checking
the prior preset remained readable. Disabling the fault before that check
fixed the test; the complete suite then passed. First-run capture:
`build/presets-hardware-first-run.log`.
These store tests use the RAM NVS mock; real-NVS capacity is a separate gate.
