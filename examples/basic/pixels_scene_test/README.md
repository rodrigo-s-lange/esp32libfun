# Scene retention gates (ESP32)

Build with `idf.py -DIDF_TARGET=esp32 build`, flash explicitly with
`idf.py -p COM6 flash`, and capture 115200-baud serial. This image replaces the
bench application temporarily. It runs Composer core/AT suites plus the real
application `scene_codec.cpp`/`scene_store.cpp` implementations.

NVS entry points are linker-wrapped in this binary only. Faults cover missing
records, deduplicated writes, newest-record selection, corrupt newest-record
fallback, torn inactive-slot write, commit/readback failures, retry, empty-scene
save and initialization failure. No NVS erase or real scene write occurs. Tests
do not simulate ESP-IDF's internal page allocator or prove physical power-loss
tolerance; actual NVS + reboot testing belongs to the root application.

Codec gates cover all meaningful fields with 32 segments, re-encoding equality,
explicit byte order, damage at every byte, length errors, CRC-valid invalid
schema/hardware/catalog/count/enums/effect/palette/source, duplicate IDs and
transactional output preservation. Substantial buffers have static lifetime.

Composer's AT gates verify restored live output/staging, no save on boot,
staging, query or cancel, exclusion of pending edits during pause save,
confirmed publish, applied-but-unsaved failure/retry, detach and validation.
All existing gates remain registered.

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
