# Next task: symmetric mirror per 1D segment

## Outcome and baseline

Implement one complete increment: enable symmetric mirror on an existing 1D
segment through AT, publish it, save/recall a named preset and retain it after
reboot. Claude implements and builds; Codex reviews, arranges observation,
runs the hardware bench and creates the local checkpoint. Do the implementation;
report concrete blockers without starting another design phase.

Start from these checkpoints, or subsequent documentation-only commits:

- Application `d6e5665845f0df580fd6e1a41680ea6690f830a3`, branch
  `checkpoint/wroom-composer-app`.
- Composer `5dc3a8ebec0e85136d28c07914e6030d25b2c370`, branch
  `feature/at-scene-persistence`, sibling `../esp_pixels_composer`.
- FX `4e977137950a30c81f4151b411b1541fba85adb4`, sibling `../esp_pixels_fx`.
- Pixels `5d7db6b7ce17935ec5213e241c66055787726564`, nested
  `framework/libs/esp_pixels`; preserve its unrelated local changes.

Read AGENTS.md and its required documents, then the relevant Composer headers,
README, renderer/configuration/AT scene bridge and application codecs/stores.
Check actual branches and working trees. Root CMake selects the sibling
Composer/FX; do not edit the old nested copies or the ignored
`pixels_composer_light_test`. Preserve Composer's unrelated untracked
`ci_harness/dependencies.lock`.

The validated baseline has 6352 passing hardware checks (6180 scene suite,
172 adapter), plus real-NVS capacity and visually confirmed virtual 5x10
mapping. Hardware: WROOM/COM6, GPIO16/50 and GPIO17/12, WS2812 RGB/GRB.
Last bench state: IDENTITY, Pos100, LED34 red, brightness96, GPIO17 black.
Exactly Pos001..Pos100 are occupied. Preserve those original records.
The accepted single green startup flash is outside this task.

## Layer and behavior

Mirror belongs to external Composer's segment composition and its AT sidecar;
durable encoding belongs to the application. Reuse existing FX and transport.
No HAL changes, new effects, custom-parameter AT work, new output/map subsystem
or 2D axis mirroring in this increment. Existing 2D scenes must remain intact.

Add an explicit segment-level `mirror` boolean, default false, independent of
`reversed` and effect parameters. Support `SegmentSpec`/`applyConfig()` and the
direct 1D construction path without breaking existing positional API calls.
Carry it through internal configuration, staged/confirmed scene export/load,
comparison/deduplication and persistence. Do not borrow `params.custom[]`.

For a 1D destination span N = stop - start, the effect sees an ordinary 1D
Surface of width M = mirror ? ceil(N/2) : N. Its retained output is the
source for composition; the destination still covers all N canvas cells.
For each destination offset d in [0,N):

```text
source = mirror ? min(d, N - 1 - d) : d
if reversed: source = M - 1 - source
color = retained_output[source]
destination = resolve canvas(start + d) through ledmap to logical output
```

This defines reverse before symmetric expansion. With mirror off it preserves
the current reverse result; with mirror on it changes the source direction
meaningfully instead of reversing an already symmetric final row.

| N | Source | Mirror | Mirror + reverse |
|---|---|---|---|
| 1 | A | A | A |
| 2 | A | A A | A A |
| 5 | A B C | A B C B A | C B A B C |
| 6 | A B C | A B C C B A | C B A A B C |

Process each destination canvas cell once. In an odd segment the center must
not receive two Add/Alpha operations. Preserve existing ledmap hole/duplicate
semantics, layer order, brightness/opacity/blend, global brightness and output
boundary routing. Keep the effect buffer/history in source coordinates; do not
expand in place and feed the mirrored destination back into FX history.
No per-frame allocation or second render task. Checked allocation during
configuration must use the effective source dimensions consistently.

Mirror is valid only when `is2D == false`, including a 1D segment over a mapped
canvas. Reject mirrored 2D specs even when h==1; 2D reflection is not defined
here. Keep the existing 2D reverse prohibition.

Include mirror in runtime identity matching: toggling it restarts that
segment's runtime/schedule/output once at adoption, even if N==1 or 2. This
prevents swapping retained buffers with incompatible source dimensions.
Unchanged mirror/geometry/effect/id/restart token must keep existing
carry-forward behavior. Reverse-only/control-only edits retain runtime as
before. Preserve cancellation, mailbox ownership and allocation-failure cleanup.

## AT and persistence

- Add `AT+SEGMIRROR=<id>,<0|1>` using the existing staged-setter pattern.
  Strictly validate ID/value/trailing arguments and 1D shape before mutation.
  It becomes live only through confirmed `AT+SEGPUB`; no immediate NVS write.
- Expose the staged flag in segment queries and discovery. It is a Composer
  control, not an FX option, and applies to any supported 1D effect.
- Re-staging an existing 1D ID with `AT+SEGSET` preserves its mirror setting.
  A new ID defaults false. Deliberate `AT+SEGSET2D` replacement clears mirror
  as well as reverse; failed replacements leave staging unchanged.
- Update registration counts/help, confirmed-scene copying/comparison, pause
  persistence and applied-but-not-saved retry paths as required by the field.

Write a new scene schema 4, continuing to read schemas 1/2/3 with mirror=false.
Keep schema 3's field positions and 39-byte segment size: replace its one-byte
reverse value with a schema-4 flags byte (bit0 reverse, bit1 mirror). Reject
unknown bits; schemas 1/2/3 must retain their strict old 0/1 reverse validation.
Never reinterpret a schema-3 byte as the new flags or change old records in
place. Keep the map descriptor, topology semantics and old effect IDs/catalog
compatible. Evolve preset wrapping/version checks only where actually needed
to support the new embedded scene schema, documenting the decision.

Preserve transactional decode outputs, validation against the stored layout,
CRC/A-B/readback/fallback behavior, incompatible-position occupancy and the
100-position/32-segment limits. Do not erase or bulk-migrate NVS.
Current maximums are scene1295 B and preset1348 B. With the flags encoding
they should remain unchanged: verify exact byte counts, NVS accounting and
the unchanged 24 KiB nvs / 320 KiB presets partition rather than assuming it.

## Focused verification and handoff

Register new tests in permanent runnable firmware, retaining the existing
6352 checks. Adjust obsolete count assertions only with the documented reason.

- Deterministic asymmetric source effect: assert effective Surface width and
  exact N=1/2/5/6 results above, both reverse values and mirror-off parity.
- Nonzero start, crossing two FakeSink outputs, progressive/serpentine ledmap
  resolution and holes. Keep existing 2D coverage; reject mirrored 2D input.
- Odd-center Add/Alpha exactly once, layering, brightness, pause/disable and
  retained history. Cover a history-reading effect/test fixture so destination
  expansion cannot silently overwrite its source buffer.
- Toggle mirror through apply/adopt, verify one restart, then verify unchanged
  configurations and reverse-only edits carry runtime forward. Failed/cancelled
  publication must preserve the live scene without leaks or buffer-size swaps.
- AT stage/query/publish, invalid inputs, shape replacement, confirmed export/
  load, staged-versus-confirmed saves and failed-save retry.
- Schema4 max-size round trips with mirror on/off and all controls; independent
  old schema1/2/3 fixtures; CRC-valid unknown flag bits and mirrored2D records
  rejected before output mutation. Scene/preset reboot and incompatibility
  behavior must remain covered.

Build application `build-wroom-rmt-safe`, expanded `pixels_scene_test` and
the independent adapter runner with ESP-IDF6.0.2, keeping RMT cache safety.
Follow main/README.md's build setup; never edit sdkconfig. The scene suite had
8248 B static DRAM free: reuse fixtures or split a runner if required, never
drop tests to fit. Report static memory and any changed runtime allocation.
Do not claim hardware execution from compilation. Run git diff --check in
each modified repo. Update public header docs and relevant READMEs.

Handoff: files/repos changed, builds, memory and exact record sizes, runnable
test additions, and a short AT bench sequence using existing Rainbow/Sinelon
to show even50/odd49 symmetry, reverse interaction, preset recall and reboot.
Include a crossing segment on the existing two outputs. Use a backed-up
existing position for temporary presets, never a 101st name. Codex will arrange
observation, preserve/restore all100 originals and finish on Pos100.

Claude restrictions: no flash, serial access, bench-backup access, commit,
push, tag, merge, rebase or unrelated cleanup. Leave implementation in the
working trees. Secondary improvements remain in the roadmap.
