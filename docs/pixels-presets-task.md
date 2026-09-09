# Next task: named pixel-scene presets

## Outcome

Add named presets to the existing WROOM application: save the confirmed
scene, list presets, load one and delete one through AT. Rodrigo specified
**100 preset positions**. Demonstrate two different saved scenes surviving
reboot and being recalled, and test the full 100-position capacity. This is the
next cohesive MVP delivery in [the roadmap](pixels-roadmap.md).

Follow `AGENTS.md` and its required reading. Start from Composer `97159d2`
and application `ca59662` or their subsequent documentation-only checkpoints;
check actual branches, HEADs and existing work before changing anything.

## Scope and behavior

- Preset names, capacity and NVS storage belong to the application. The
  Composer may receive the smallest storage-neutral scene-bridge extension
  needed to access confirmed scenes and load them through its sidecar.
  Do not add NVS, JSON or preset policy to the core or Composer engine.
- Proposed commands: `AT+PRESETS?`, `AT+PRESETSAVE=<name>`,
  `AT+PRESETLOAD=<name>`, `AT+PRESETDEL=<name>`. Register under an explicit
  application library for discovery; document syntax and responses.
- Save the last confirmed segments with current global brightness/pause,
  excluding unpublished staged edits. Save to an existing name replaces
  that preset deliberately; preserve the prior valid record on failure.
- Load validates a whole preset before use and replaces the staged table
  and active scene, including global brightness/pause. Loading deliberately
  discards pending staged edits; document that behavior. Use the existing
  confirmed publication path and save the resulting current scene for reboot.
- Validate before mutation. Corrupt/missing/incompatible presets leave the
  active scene and staged table unchanged. Distinguish application failure
  from an applied scene whose subsequent current-scene save failed.
- Queries and deletion do not change the active scene. Deleting a preset
  does not erase the independently retained current scene.
- Provide 100 preset positions with documented name rules. Empty positions
  need not contain allocated scene records. Size storage for 100 occupied
  positions at the maximum supported scene size, including names, journal
  redundancy, NVS overhead and space for replacement/recovery. Test full
  capacity, replacement while full and rejection beyond 100 without data loss.
- The current standard partition table has only 24 KiB of NVS. A schema-1
  scene can occupy 1124 bytes; 100 maximum-sized scenes already require
  112400 bytes before names, redundancy or NVS overhead. Include the necessary
  application partition/storage configuration in this task, using a custom
  partition CSV and sdkconfig.defaults as needed, never editing sdkconfig.
  Preserve the existing current-scene NVS partition/data when arranging
  preset storage. Report the proposed flash layout and capacity calculation
  before the separately authorized bench flash; do not silently reduce 100.
- Preserve current-scene A/B recovery and deduplication. Reuse the explicit
  scene codec where appropriate; preset metadata may have its own version.
  Prepare a clear compatibility boundary for later 2D/multiple-output scenes,
  without implementing those features in this task.
- Keep the Composer task managed and `app_main()` returning. Respect the
  single control caller and borrowed-cache lifetime. Do not read the bound
  confirmation cache from application code or invoke callbacks reentrantly.
  `attachAtPersistence()` is a startup helper, not a runtime preset loader.

## Verification and delivery

Extend the existing suites for two named scenes, replacement/deletion,
capacity and invalid names, corrupt records, storage failure/retry, exclusion
of staged edits on save, and confirmed load plus current-scene retention.
Exercise observable fields/output and callbacks, not just `feedLine()`'s
dispatch return value. Keep existing tests registered.

Build the application and scene test project for ESP32 with ESP-IDF 6.0.2;
run `git diff --check`. Report implementation choices, files, build results
and a short bench sequence. Update public documentation with actual behavior.
Do not claim execution or visual confirmation from a successful build.

No flash, serial, commit, push, tag, merge, rebase, direct sdkconfig edits,
FX/Pixels changes or unrelated cleanup in this implementation task. If a
functional blocker requires expanding scope, report the concrete blocker.
Codex will execute the suite and the operator-approved visual sequence, fix
concrete failures, and checkpoint the validated delivery.
