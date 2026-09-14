# Next task: 2D canvas / ledmap through AT and persistence

## Outcome and baseline

Deliver one usable 2D MVP increment: configure a rectangular pixel matrix
through AT, publish a real XY segment, save/recall it as a named preset, and
restore it after reboot. Keep the implement/build/bench/checkpoint cycle:
Claude implements and builds; Codex reviews, runs the separately arranged
physical bench and checkpoints the result. Do the work, not another design
phase. Report only concrete blockers; secondary improvements stay in the roadmap.

Start from these code checkpoints, or subsequent documentation-only commits:

- Application: `5361eb090a9abb4640326074028d3d81c24c1cea`, branch
  `checkpoint/wroom-composer-app`.
- Composer: `07eb426a9c72f4b53062ba62d3a9a63370534755`, branch
  `feature/at-scene-persistence`, sibling `../esp_pixels_composer`.
- FX: `e7e0ba391473005137f6fd4dcf70b4355975b6ac`, sibling `../esp_pixels_fx`.
- Pixels: `5d7db6b7ce17935ec5213e241c66055787726564`,
  `framework/libs/esp_pixels`; preserve its existing local README/AT changes.

Read AGENTS.md and its required documents, then the relevant headers/READMEs
and implementation in Composer, its AT scene bridge, FX and `main/`.
Check actual branches, revisions and working trees before editing. Root CMake
selects the sibling Composer/FX; do not use the older nested Composer copy or
modify the ignored `pixels_composer_light_test` project.

The WROOM/COM6 has GPIO16/50 and GPIO17/12 WS2812 RGB pixels, both GRB.
It holds exactly Pos001..Pos100, with Pos100 active (physical LED 34 red,
brightness 96, second strip black). Preserve all names and original records.
5652 hardware checks passed (5480 scene/AT/preset/output + 172 adapter), plus
201 maximum-size real-NVS writes and all 100 legacy recalls. The startup green
flash remains an accepted, unresolved issue; do not reopen it in this task.

## Layers and smallest implementation

- Application: matrix configuration, AT configuration commands, boot order,
  device compatibility and NVS codecs/stores.
- External Composer AT sidecar: 2D segment commands and full-scene
  validation/export/load. Reuse its existing `SegmentSpec::is2D`, XY geometry,
  `setLedmap()`, `applyConfig()` and confirmed-publish path.
- External FX library: one minimal spatial effect needed for acceptance.
  Solid already supports both dimensions, but a uniform fill alone cannot
  demonstrate correct XY addressing.
- No new HAL subsystem, transport rewrite or second rendering engine.

### Matrix configuration

Support the existing IDENTITY default and a rectangular matrix generated from
`width`, `height`, `first logical pixel`, and row wiring mode:
PROGRESSIVE or SERPENTINE. Define origin at top-left, X right, Y down; in
SERPENTINE, row 0 runs forward and odd rows run backward. The generated table
maps canvas row-major indices to the existing global logical pixel indices.
`first` is not a per-output index. A map may cross the physical-output boundary;
pixels outside its mapped range remain black when a nonempty scene renders.

This increment needs no arbitrary map-entry editor, holes, panel tiling,
rotation UI or separate canvas per output. The Composer's existing capabilities
remain available in C++; these are limits of this first product AT increment.
Generate the table from a compact descriptor, rather than persisting a full
table in every preset. Validate positive dimensions, multiplication/addition
overflow and `first + width * height <= totalPixels()` before mutation or
allocation. Keep existing 1D/output limits; report the map buffer cost and
allocation failure cleanly instead of promising every possible scene fits RAM.

Suggested application library: `PIXELS_MAP`, with:

- `AT+MAP?`: show active, saved-for-next-boot and staged configuration,
  dimensions, wiring mode and global logical range.
- `AT+MAPSET=<width>,<height>,<first>,<PROGRESSIVE|SERPENTINE>`: stage only.
- `AT+MAPCLR`: stage IDENTITY; does not clear a scene or write storage.
- `AT+MAPSAVE`: validate and persist; effective on the next explicit reboot.

Use the existing AT discovery/registration pattern. Mapping is fixed for a
Composer lifecycle: build the sink/Composer, set the map before attaching a
scene or rendering/starting, then restore the scene. Do not call `setLedmap()`
from the running AT task or add live lifecycle replacement for this delivery.
Keep `app_main()` returning and preserve startup black on both outputs.

Keep A/B, version, CRC and readback protection for durable map configuration.
Output and map saves must validate the combined saved-for-next-boot topology,
not the other command's unsaved staged candidate. Extend the current-scene
guard to both commands: reject a save that invalidates the durable nonempty
scene; explicit clear/publish is the user's escape route. Subsequent scene
saves must also respect pending topology changes. Cover both save orders.
At boot, validate the scene against the configuration actually selected after
any journal fallback; never reinterpret it silently under an identity fallback.

### 2D segments and effect

Add `AT+SEGSET2D=<id>,<x>,<y>,<width>,<height>,<effect_id>` to the Composer
sidecar. Keep `AT+SEGSET` and existing controls compatible. Both commands stage
only, share IDs and the 32-segment budget, and become live through `SEGPUB`.
Queries must distinguish 1D/2D and show the relevant geometry. Deliberate
replacement of an ID with the other shape must set the discriminator/geometry
correctly, validate before mutation and follow existing runtime identity rules.

Reuse color, brightness, blend, opacity, state, speed and palette controls where
the effect supports them. Preserve the existing shape/dimension rules, including
height-one surfaces and the rejection of reversed 2D specs. Mixed 1D/2D segments
must compose using the same canvas and layer order. Under a matrix, 1D ranges
are flat row-major canvas indices; only the ledmap resolves them to physical
pixels. Update misleading comments that equate canvas and physical indices.

Extend the AT scene bridge's current 1D-only validation/export/load, not just
the command parser. Reject invalid bounds, dimensions, IDs and effect-shape
combinations without modifying staged/active state. Keep cancellation,
confirmed publication and applied-but-not-saved reporting intact.

Add one small, asymmetric XY effect (for example an XY gradient) that visibly
depends on both coordinates. Follow the existing FX registration, Surface,
fixed-point and control-metadata conventions; no per-frame allocation or new
work-buffer subsystem. Append a stable effect ID without renumbering old IDs
or invalidating old records merely because the catalog gained an effect.
Do not just mark Rainbow/Sinelon as 2D-capable. Test the real new FX rendering
at several coordinates on a nonsquare surface.

## Scene/preset persistence and compatibility

Evolve scene encoding deliberately (a new schema), preserving reads of schemas
1 and 2 and all existing segment/control values. Encode a shape discriminator
and only the active geometry fields, plus the compact layout descriptor needed
to reconstruct/validate its meaning. Do not serialize raw C++ structs, redundant
geometry, pointers, full generated ledmap tables or effect runtime.

A preset describes a scene for a compatible device layout. Recall must not
reconfigure pins or change the active map. New 2D records require compatible
output topology and the same canvas/map semantics. Old 1D records imply the
identity canvas: allow them only when the physical output prefix and flat
mapping of their original canvas domain remain unchanged. Otherwise return an
actionable incompatibility error before any staged/live mutation. Returning to
IDENTITY with the original output prefix must make all 100 old presets usable.

Keep structurally valid incompatible presets listed and their positions
occupied. Do not fall back to an older compatible A/B preset while a newer
valid incompatible record exists. Preserve failed-load output arguments,
duplicate-ID validation, bounds against the stored layout, truncated-record
checks, replacement safety and the current-scene journal's documented fallback.
No automatic erasure, bulk migration, freeing incompatible positions or silent
scene clearing. Empty publication retains the last physical frame; use explicit
Disabled segments when a bench step needs an immediate blackout.

Keep 100 positions, 32 segments and existing partition offsets/layout:
24 KiB `nvs` and 320 KiB `presets`. Calculate the new worst-case scene and
preset sizes before expanding buffers. Check 100 full A/B positions, NVS blob
overhead, namespace/metadata and a garbage-collection reserve. Prefer the compact
tagged geometry/descriptor above; do not lower capacities or change partitions
to avoid sizing work. If the calculated bound cannot fit, report the concrete
numbers. Real-NVS capacity execution remains a separate Codex bench gate.

## Verification and handoff

Add focused gates and ensure they are registered in a built runnable suite:

- Exact progressive/serpentine mappings on a nonsquare matrix, offset,
  overflow/range rejection, and routing across two FakeSink output slices.
- Actual new effect XY output; 2D AT stage/query/publish, invalid-input
  preservation, shape replacement, mixed 1D/2D composition and controls.
- Confirmed scene export/load, new-schema maximum-size round trips, old
  schema compatibility, CRC-valid malformed/truncated records and failed-load
  preservation. Incompatible presets remain counted and available later.
- Output/map save ordering, current-scene/pending-topology guards, A/B
  corruption/failure/retry and simulated reboot restoration.

Keep the existing 5652 checks registered; changes to obsolete assertions must
be justified by the new documented contract. Avoid another oversized static
fixture: the scene suite currently has about 19.9 KiB static DRAM margin but
also allocates its preset mock at runtime. Report static and runtime memory
costs honestly; split a runner or reuse fixtures if necessary, without dropping
gates. Keep the independent adapter runner working.

Build the application and all affected test firmware with ESP-IDF 6.0.2 for
ESP32, following `main/README.md`'s fresh-config `build-wroom-rmt-safe` procedure
and retaining `CONFIG_RMT_TX_ISR_CACHE_SAFE=y`. Do not edit sdkconfig directly.
Run `git diff --check` in every changed repo. Update the relevant public headers,
READMEs, discovery help and schema/compatibility documentation.

Handoff: changed files/repos, builds, memory and record-size accounting, added
gates, and exact AT commands for the bench: explicit scene clear if needed,
configure/save/reboot a matrix, spatial XY pattern, progressive versus
serpentine routing, 2D preset save/recall and reboot, then restore identity and
all original presets. The 12 pixels on GPIO17 could form a 3x4 demonstration;
actual physical arrangement is agreed with Rodrigo by Codex, not assumed.
Preserve multiple-output coverage with a crossing map in automated tests.

Claude's implementation handoff: no flash, serial, commit, push, tag, merge,
rebase or unrelated cleanup. Do not access the bench backups or temporarily
overwrite device presets. Codex handles verified backup/restore, real-NVS
capacity, operator observation and local checkpoints. No broad mirror, custom
parameter AT, additional effects or CH32 port in this task; the first three
remain in the roadmap, and CH32 was exploratory only.
