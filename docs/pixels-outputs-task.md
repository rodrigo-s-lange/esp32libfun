# Next task: multiple physical pixel outputs

## Outcome and baseline

Deliver two independently driven RGB strips through the existing Composer,
with output configuration through AT, reboot retention and named presets.
This is the next MVP delivery; keep the same implement/build/bench/checkpoint
cycle. Claude implements this brief; Codex integrates and runs the bench.

Start from application `08966f65f297bf62b73a10d04f1b0b1ff01087f6`
(`checkpoint/wroom-composer-app`) and Composer
`0730626d2842a55d980b0848e636b579ecc81c62`
(`feature/at-scene-persistence`), or subsequent documentation-only commits.
Read AGENTS.md and the relevant component headers/READMEs. Check actual
branches, revisions and existing changes before editing.

The device currently has 100 real presets, `Pos001` through `Pos100`, and
Pos100 active (physical LED 34 red, global brightness 96). Preserve them.
The one-time green flash during startup remains unresolved and Rodrigo
explicitly accepted proceeding on 2026-09-11. Do not make that investigation
a prerequisite for this delivery or claim a confirmed hardware cause.

## Smallest complete delivery

- Extend the external `adapter_esp_pixels` to drive at least two `Pixels`
  instances. The Composer engine already supports multiple output slots;
  reuse its contiguous logical ranges and existing frame lifecycle.
  Preserve the current single-output initialization API. Document the
  adapter's actual limit and RMT resource use; `MAX_OUTPUTS == 8` is not
  a promise of eight physically available channels on every chip.
- The adapter owns configuring/releasing its transports and staging buffers,
  as today. Reject duplicate pins/transport instances and invalid layouts
  before mutation. On partial initialization failure release only resources
  acquired by that attempt. Commit/wait must drain every transmission that
  started, including when another output fails; never reuse or free an
  in-flight buffer. Keep synchronized_start false unless implemented.
- Output pins, counts and RGB byte order are application/device configuration,
  separate from effect segments. Add an application-owned `PIXELS_OUTPUTS`
  AT library. Suggested commands: `AT+OUT?`, `AT+OUTSET=<slot>,<pin>,<count>,<order>`,
  `AT+OUTDEL=<slot>`, `AT+OUTSAVE`. Document indexing and discovery. Query
  distinguishes active and staged/saved configuration. Set/delete stage only.
- For this delivery, saving output configuration takes effect on the next
  explicit reboot. State that in the success response; do not silently reset
  or claim immediate application. Live topology replacement is not required.
  Validate the whole candidate and preserve the previous valid configuration
  on failure. Do not implicitly discard the current scene to make it fit.
- Default boot remains the existing GPIO16/50/GRB output. A second GPIO is
  activated only by explicit configuration. Wiring/count for the second strip
  will be agreed with Rodrigo during bench setup, after implementation.
- Retain device output configuration with a versioned, CRC-protected A/B
  journal and readback, following the existing application pattern. Initialize
  all configured sinks, send startup black, establish topology and restore the
  current scene before starting the managed task. Keep app_main() returning.
- Segments retain global logical indices: for counts 50 and 20, output 0 is
  [0,50), output 1 is [50,70). Demonstrate independent segments and a segment
  crossing that boundary. No second numbering convention in SEGSET.

## Persistence and compatibility

Evolve the application scene/preset format deliberately; keep storage and
physical-pin policy out of the HAL core and transport-neutral Composer.
Named presets describe scenes for a compatible active device topology;
loading a preset must not unexpectedly reassign physical pins. Saving a
preset uses the active topology, never an unpublished output configuration.

Continue reading the existing schema-1 current scene and all 100 presets.
Support recalling those old single-output presets when the original first
output is unchanged and additional outputs have been appended: the original
50 pixels keep their meaning and the added outputs are black. Reject other
incompatible records before changing live/staged state, with an actionable
error; keep the stored record available. Do not bulk-rewrite records at boot
or erase NVS as migration. Preserve names, replacement safety and capacity 100.

Recalculate maximum record size and A/B NVS overhead for the supported output
limit and 32 segments. Keep the existing partition offsets/data and use the
320 KiB preset partition if it fits. Report a concrete blocker if a layout
change is necessary. Do not edit sdkconfig directly.

## Verification and handoff

Extend meaningful gates for two physical-adapter outputs, wire order, boundary
routing, partial-failure draining/cleanup, staged output configuration, reboot
retention, old-record compatibility and incompatible-load preservation.
Keep the existing 5224-check suite registered; assert state/output and storage
effects, not only command-dispatch success. Test maximum-size preset capacity
and replacement with the new format; distinguish a RAM mock from real NVS.

Build the application and relevant test firmware for ESP32 with ESP-IDF 6.0.2,
using the documented fresh-config build procedure and keeping
CONFIG_RMT_TX_ISR_CACHE_SAFE enabled. Run git diff --check in changed repos.
Update public docs with commands, logical ranges, compatibility and limits.
Report files, builds, record-size calculation and the concrete bench sequence:
two strips with different colors/effects, a crossing segment, preset recall,
reboot, and all 100 legacy presets still available.

Implementation handoff: no flash, serial, commit, push, tag, merge, rebase or
unrelated cleanup. No 2D/mirror/new-effects expansion in this task; those stay
in the roadmap. Codex performs the separately arranged physical bench work
and checkpoints the result. Report only concrete scope blockers.
