# Scene retention gates (ESP32)

Latest hardware run (2026-09-22, custom parameters/Stripes): **7088 checks,
zero failures** on WROOM/COM6. Binary 592352 B; static DRAM 178528/180736 B
(2208 B free). The independent adapter runner adds 172 passing checks.
The first run caught invalid 1D codec fixtures selecting 2D-only XYGradient;
the fixtures now include Stripes among the five 1D-capable IDs and abort
their record-corruption checks if initial encoding fails. No production
codec change was needed. Stripes now also tests phase 255 together with
both widths at 255. Captures: root
`build-wroom-rmt-safe/bench-custom-20260922/scene-fixed-tests.log` and
`adapter-tests.log`.

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

### Multiple physical pixel outputs — hardware gates passed 2026-09-14

`scene_codec.*`/`scene_store.*`/`preset_codec.*`/`preset_store.*` were
extended for schema 2's own topology-aware header (see `main/README.md`'s
own "Multiple physical pixel outputs" section for the full contract); this
image was rebuilt against them and now additionally links a new
`test_outputs.cpp` plus the real application `output_store.cpp`/
`outputs_at.cpp`. The WROOM/COM6 run passed **5480 checks, zero failures**:
1605 Composer, 1053 AT, 1246 scene, 1508 presets and 68 outputs. The checked
mock allocation succeeded on hardware. Two erroneous test expectations
were corrected before the final run: a schema-2 brightness byte can be 255,
and an empty preset has both inner and outer CRC32. No production code was
changed to satisfy these assertions. Capture:
`build-wroom-rmt-safe/bench-outputs-20260914/scene-tests-final.log` in the
application repository. The separate real-adapter suite also passed 172
checks on GPIO4/5; see `examples/basic/pixels_adapter_test`.

- `test_scene.cpp`'s `codecTests()` gained a global-logical-index
  demonstration: two outputs (counts 50 and 20), one segment entirely on
  each output plus one segment crossing the `[50,50)` boundary (`[45,55)`),
  all validated only against the combined `totalPixels()` (70) -- proving
  "no second numbering convention in `AT+SEGSET`" directly. `journalTests()`
  gained three compatibility gates through `SceneStore` itself (not just the
  codec): a real single-output scene recalled correctly after a second
  output is appended (the original segment keeps its exact meaning, the
  added output is simply uncovered), and the same record rejected -- via a
  direct `decodeScene()` probe of the identical mock bytes a real reboot
  would read, not a second `SceneStore` instance -- when the FIRST output's
  own pin changes instead.
- New `test_outputs.cpp`: `OutputStore`'s own A/B journal (load/save,
  dedup-on-unchanged, torn-write fallback, write/commit/readback
  failure-then-successful-retry, an invalid candidate rejected before any
  write, shared `g_scene_init_error` for `nvs_flash_init()` failure -- see
  its own top comment for why that lever is shared with `SceneStore`), then
  one continuous three-phase `outputsAt` AT-library session reusing a
  single "device" `OutputStore` instance the way one real boot would
  (`AT+OUTSET` staging with four different rejected edits proven not to
  have altered the staged candidate, `AT+OUTDEL` shifting slots down and
  refusing to remove the last one, and a failed `AT+OUTSAVE` leaving the
  previously durable configuration exactly as it was) -- each phase's own
  persisted result independently re-verified through its own fresh
  `OutputStore` instance (`load()` is call-once-per-instance, mirroring a
  real reboot).
- `main/nvs_mock.hpp`/`.cpp` gained a third mock target for `OutputStore`'s
  own `outputs_a`/`outputs_b` keys/`"pixels_outputs"` namespace, dispatched
  by a now namespace-aware `__wrap_nvs_open()` (handle 1 = scene, 2 =
  presets, 3 = outputs) since `nvs_commit()` itself carries no key to
  dispatch by.
- The implementation initially left only 56 bytes of link-time DRAM margin.
  Review moved the 100 x 2 preset-mock array to one checked allocation at test
  startup; no gates were removed. This frees static placement space, not the
  total runtime cost of the mock: its allocation must still succeed on the
  device. The final reviewed build has 19944 bytes of link-time DRAM margin
  (88.97% used; see `build/outputs-review-size.log`).
  Maximum-size (32-segment, `kMaxOutputs`==2) preset round
  trip stays a direct `encodePreset()`/`decodePreset()` call with no store
  or mock involved, deliberately: the preset mock's own per-key buffer
  (160 bytes) is sized only for the small 0-2 segment scenes every
  `PresetStore`-level gate actually uses.

### 2D canvas / ledmap — built and executed on WROOM

`scene_codec.*`/`scene_store.*`/`preset_codec.*`/`preset_store.*` were
extended again for schema 3's own map-aware header (see `main/README.md`'s
own "2D canvas / ledmap" section for the full contract); this image was
rebuilt against them and now additionally links a new `test_map.cpp` plus
the real application `map_store.cpp`/`map_at.cpp`. Build and hardware
execution passed; see the review-integration counts below.

- `test_scene.cpp`'s `codecTests()` gained a real (non-identity) 5x14
  matrix round trip over the same two-output, 70-pixel topology used by the
  boundary-crossing demonstration: one 2D segment filling the whole canvas
  mixed with one 1D segment addressing the flat row-major canvas space
  directly, both decoded back correctly; the identical bytes rejected
  outright under the identity canvas (`isCompatibleMap()` requires an EXACT
  match once a record's own map is a real matrix); and a 2D segment one
  column past the matrix width rejected at encode time. `journalTests()`
  gained a fourth compatibility gate, probing the SAME real single-output
  record the existing changed-pin gate already uses: installing a serpentine
  matrix as the CURRENT map is incompatible for that old identity record,
  while a zero-offset progressive matrix covering its domain is compatible -- proven
  through the identical direct-`decodeScene()`-probe technique, no new
  `SceneStore` instance.
- New `test_map.cpp`, mirroring `test_outputs.cpp`'s own structure exactly:
  `MapStore`'s own A/B journal (load/save, dedup-on-unchanged, torn-write
  fallback, write/commit/readback failure-then-successful-retry, a
  structurally invalid candidate rejected before any write), then one
  continuous three-phase `mapAt` AT-library session reusing a single
  "device" `MapStore` instance the way one real boot would (`AT+MAPSET`
  staging a real matrix bounded by the currently-saved output topology,
  rejecting an out-of-bounds candidate without altering the staged
  candidate, `AT+MAPSAVE` persisting it; `AT+MAPCLR` reverting to identity;
  a failed `AT+MAPSAVE` leaving the previously durable configuration
  exactly as it was) -- each phase's own persisted result independently
  re-verified through its own fresh `MapStore` instance.
- `main/nvs_mock.hpp`/`.cpp` gained a fourth mock target for `MapStore`'s
  own `map_a`/`map_b` keys/`"pixels_map"` namespace (handle 4), the same
  namespace-aware `__wrap_nvs_open()` dispatch the output mock already
  established.
- Schema 3's own header (+7 bytes, the map descriptor, always present) and
  per-segment layout (+5 bytes: a 1-byte shape discriminator plus `w`/`h`;
  2D reuses the existing `start`/`stop` bytes as `x`/`y`, so there is no
  redundant second geometry pair) grow `kSceneRecordBytes`/
  `kPresetRecordBytes` further still (see `main/README.md`'s own
  recalculation). This image's own link-time DRAM margin is **95.43% used,
  ~8.1 KiB free** -- tighter than the previous delivery's own reviewed
  19944-byte margin (the growth this time crosses into a new NVS-entry
  rounding bucket too, see `main/README.md`), but still fits with no
  further static-instance trimming needed. Any future addition to this
  specific binary should re-measure this margin before assuming it still
  fits.

### 2D review integration (2026-09-15)

The permanent runner now invokes `run_xy_gradient_tests()` and reports its
result/count together with the existing suites. Composer AT gates include
mixed 1D/2D scenes, two-output mapping, shape replacement and invalid-input
preservation. Map gates cover both save orders, pending-layout guards and a
2D scene journal reboot. Independent schema-1/2 byte fixtures check continued
compatibility without using the new encoder to create the old format.

Reviewed build: 544144-byte binary; 172488 bytes static DRAM (95.44%), with
8248 bytes remaining. Additional test fixtures are checked temporary heap
allocations, not more persistent static instances. Build and WROOM/COM6
execution passed: 6180 checks, zero failures, including the allocation gates.
The independent adapter runner passed 172 additional checks. Build log:
`build/2d-review-build.log`; serial logs in the root application's
`build-wroom-rmt-safe/bench-2d-20260915/`. No existing suite was removed.

### Symmetric 1D mirror — built, not yet executed on WROOM (21/09/26)

`scene_codec.*`/`preset_codec.hpp` gained schema 4 (see `main/README.md`'s
own "Symmetric 1D mirror" section for the full contract); this image was
rebuilt against them. `preset_store.*`/`scene_store.*`/`main.cpp` needed no
changes (mirror adds no new store or boot-sequence concept).

- `test_scene.cpp`'s `codecTests()` now varies `mirror` (`i % 3 == 0`)
  across the existing 32-segment maximum-size round trip, alongside
  `reversed` (`i % 2 != 0`) -- every one of the four flags-byte combinations
  appears at least once. Beyond the existing byte-exact round-trip/fuzz/
  size-boundary checks (now implicitly schema-4, and re-asserted per-segment
  for `mirror`), new checks confirm: the schema field is genuinely `4`; the
  SAME bytes, read back with only the schema field changed to `3`, are
  rejected (a mirror-only flags byte, `2`, is not a valid strict-0/1 schema-3
  value) -- then, with the mirror bit stripped from every segment, the same
  bytes decode correctly AS schema 3, with `mirror` false throughout and
  `reversed` intact; `encodeScene()` rejects a 2D segment with `mirror=true`
  the same way it already rejects one with `reversed=true`; and a raw,
  hand-crafted record with `is2D` and the mirror bit both set is rejected on
  DECODE before any output mutation. `legacySchemaTests()`'s existing
  schema-1/2 raw-byte fixtures now explicitly assert `mirror == false`.
- `test_presets.cpp`'s own maximum-size preset round trip (`kPresetRecordBytes`,
  32 segments, two outputs) gained the same `mirror` variation and a
  per-segment survival check through the full preset (opaque-blob) round
  trip -- `record.size == bench::kPresetRecordBytes` passing unchanged
  confirms the 1348-byte maximum really did stay size-neutral, not just by
  assertion in `main/README.md`.
- `esp_pixels_composer`'s own `ci_harness` (`test_composer.cpp`/
  `test_composer_at.cpp`, linked into THIS image too) gained the full
  Composer-side mirror gate set: worked-example table, source-width probe,
  center-composed-once, history-buffer isolation, boundary-crossing ledmap,
  mirrored-2D rejection, exactly-one-restart, and the full `AT+SEGMIRROR`
  stage/publish/persistence set -- see `esp_pixels_composer/test/README.md`
  for the exact list.

This delivery's own link-time DRAM margin dropped further, from the prior
8248-byte free baseline to **2224 bytes free (178512/180736 used, 98.77%)**
-- a real, build-verified, still-positive margin. One large static
`AtScene` pair (in a single new AT persistence gate) was converted to heap
(`std::nothrow`) once discovered during this delivery's own build, matching
this suite's already-established pattern for large scratch objects; no
coverage was dropped to fit. **Executed on WROOM: 6588 checks, zero failures.**
Two test setups were corrected after the first execution: wait for render
adoption before checking accepted 2D configurations, and capture a prefix of
the large sink submission in the allocation-failure test. Production Mirror
code did not change. The independent adapter added 172 passing checks.
Logs: `build-wroom-rmt-safe/bench-mirror-20260921/` in the application root.
The application preset handlers also dropped redundant whole-scene temporary
resets after a real AT-task stack overflow during legacy preset recall. The
suite passed again with that fix; final binary size: 575792 bytes.
Any future addition to this specific binary
should re-measure this margin before assuming it still fits; there is
markedly less headroom left than the previous two deliveries reported.

### Usable custom effect parameters — built, not yet executed on WROOM (22/09/26)

No `scene_codec.*`/`preset_codec.*` change: `FxParams::custom[3]` was
already carried through every schema's own encode/decode loop before this
delivery (verified against the actual code, not assumed -- see
`main/README.md`'s own "Persistence" note). This image was rebuilt only to
link the new `esp_pixels_fx` effect (`Stripes`, id 5) and the new Composer
AT setters against it.

- New `${BENCH_ROOT}/../esp_pixels_fx/test/test_stripes.cpp` wired into this
  image's own `main/CMakeLists.txt`/`main.cpp` (`run_stripes_tests()`/
  `stripes_check_count()`, same pattern `test_xy_gradient.cpp` already
  established): metadata, the task brief's own worked example, phase
  shifting by exactly one cell, boundary custom values, source width 1, a
  non-period-multiple width, and combined complete-redraw/time-independence.
- `test_scene.cpp`'s `codecTests()` and `test_presets.cpp`'s own maximum-
  size round trip now cycle `effect_id` through the five 1D-capable IDs
  (0, 1, 2, 3, 5), including Stripes. XYGradient requires 2D geometry and
  cannot be used in these 1D fixtures;
  `test_presets.cpp` additionally varies `custom[0..2]` per segment with an
  explicit per-segment survival check -- `record.size` staying exactly
  `kPresetRecordBytes` throughout the whole 32-segment round trip confirms
  the 1348-byte maximum really did stay unchanged.
- `esp_pixels_composer`'s own `ci_harness` (`test_composer.cpp`/
  `test_composer_at.cpp`, linked into THIS image too) gained the full
  custom-parameter gate set: two real-`Stripes`-effect composition
  integration gates (mirror/reverse fold, two-output crossing) and the full
  `AT+SEGCUSTOM1/2/3` stage/publish/persistence/shape-neutral set -- see
  `esp_pixels_composer/test/README.md` for the exact list.

This delivery's own link-time DRAM margin barely moved: from the Mirror
delivery's own bench-verified 178512/180736 bytes used (2224 free) to
**178528/180736 bytes used (2208 free, 98.78%)** -- only 16 bytes consumed
despite four files' worth of new registered coverage, because every new
large scratch object was heap-allocated (`std::nothrow`) from the start
rather than added as a new `static`; no coverage was dropped to fit.
**Executed on WROOM: 7088 checks passed.** Final binary size after review
fixes: 592352 bytes (original handoff build: 592272 bytes).
Any future addition to this specific binary should re-measure this margin
before assuming it still fits; the margin is now even tighter than the
Mirror delivery's own already-tight report.

### Blink, Breathe and Chase — reviewed and executed in QEMU, WROOM pending (27/09/26)

No `scene_codec.*`/`preset_codec.*` change: `effect_id` is an opaque byte,
never validated against a known-ids list at the codec level (verified
against the actual code, not assumed -- see `main/README.md`'s own
"Persistence" note). This image was rebuilt only to link the three new
`esp_pixels_fx` effects and their tests against it.

- New `${BENCH_ROOT}/../esp_pixels_fx/test/{test_blink,test_breathe,
  test_chase}.cpp` wired into this image's own `main/CMakeLists.txt`/
  `main.cpp` (`run_blink_tests()`/`run_breathe_tests()`/`run_chase_tests()`
  + their own `_check_count()`, same pattern `test_stripes.cpp` already
  established): exact timing boundaries, normalized `sin8()` blend curve,
  spatial motion direction/wrap, `speed==0`/`speed==255` extremes, real 2D
  rendering, complete redraw, independent runtimes, restart determinism,
  repeated-timestamp and clock-rollover safety -- see
  `esp_pixels_fx/README.md`'s own `v0.2.3` entry for the full per-effect
  list.
- `test_scene.cpp`'s `codecTests()` and `test_presets.cpp`'s own maximum-
  size round trip now cycle `effect_id` through all eight registered
  1D-compatible ids (`i % 8`, computed from `sizeof(effects1D)` rather than
  a repeated magic number, so a future effect addition cannot silently
  desync the two).
- `esp_pixels_composer`'s own `ci_harness` (`test_composer.cpp`/
  `test_composer_at.cpp`, linked into THIS image too) gained four new
  integration gates: Chase through Mirror/reverse and a moving two-output
  crossing, a real 2D Blink segment through an actual `setLedmap()` canvas,
  and a combined AT-publish/persistence round trip proving both new effect
  ids and their own distinct parameters survive -- see
  `esp_pixels_composer/test/README.md` for the exact list.

This delivery's own link-time DRAM margin barely moved: from the custom-
parameters delivery's own bench-verified 178528/180736 bytes used (2208
free) to **178568/180736 bytes used (2168 free, 98.8%)** -- only 40 bytes
consumed by three effects' worth of unit tests plus four Composer
integration gates, none adding a new `static`; no coverage was dropped to
fit, no runner split was needed. Codex's review found and fixed two concrete
effect defects before hardware: Breathe now reaches color1 exactly at the
start/full-cycle boundary, and Chase uses exact milli-cell accumulation so
different render cadences reach the same phase. The complete image executed
in ESP32 QEMU with **7490 checks, zero failures**. WROOM execution remains
pending because COM6 enumerates but the target is not responding to reset.
Binary size: 610832 bytes. Any future addition to this specific
binary should re-measure this margin before assuming it still fits.
