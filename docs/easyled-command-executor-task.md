# Next task: offline EasyLED command executor

## Outcome and ownership

Implement one application-layer command executor for the provisional EasyLED
contract. It must turn a valid `apply_config` or `activate_preset` envelope and
its exact payload bytes into a validated pixel candidate, drive the existing
durable transaction interfaces in the required order, and emit the expected
events. This is the next cohesive increment toward the 2026-10-15 delivery.

Layer: application/example code under
`examples/basic/pixels_network_session/components/`. Do not add a framework
core module and do not edit `easyled-platform`. Reuse the existing
`pixels_firmware_contract`, Composer, FX metadata and network-session APIs.
The executor must remain independent of MQTT callbacks, HTTP clients, clock
selection and the final NVS key/capacity policy through narrow injected
interfaces.

Claude implements and builds. Codex reviews, runs QEMU and hardware where
useful, fixes concrete failures and creates local checkpoints. Restrictions
for Claude: no flash, serial, bench-backup access, commit, push, tag,
merge/rebase or unrelated cleanup.

## Confirmed baseline

- Application/root `8e9c475f30784011d814711706353cb5d8fe5899`, branch
  `checkpoint/wroom-composer-app`.
- Composer `17b5e9a6ef6e9701b1336bfd9cc03470173c8449`, sibling
  `../esp_pixels_composer`, branch `feature/at-scene-persistence`.
- FX `9de81da4848df09be3b2c5aa4156a1e160ceb84c`, sibling
  `../esp_pixels_fx`, branch `integration/v0.2.0`.
- The offline contract boundary is already in root commit `bafd8ad`: bounded
  command parsing, exact-byte SHA-256, event serialization,
  `TerminalJournal`, `DurableStaging`, and 52 passing QEMU checks.
- Pixel application features through FX ids 0..8 are checkpointed. On
  WROOM/COM6 the scene runner passed 7490 checks and the adapter passed 172,
  both with zero failures. Rodrigo confirmed Blink/Breathe/Chase visually on
  28/09/26. Current bench: GPIO16/50 GRB, identity map, no active scene,
  disposable preset cache 0/100; GPIO17 is physically inactive.

Preserve unrelated generated/untracked harness artifacts. Use the sibling
Composer/FX repositories selected by root CMake, never an older copy.

## Provisional fixtures to version

Move the exact fixture strings already embedded in
`validation/main/main.cpp` into inspectable files under
`validation/fixtures/`, with no trailing newline where the supplied checksum
requires that. At minimum version:

- `apply-config-command.provisional.json`;
- `config-payload-bench-gpio16-50.provisional.json`, SHA-256
  `b62868aef25369d8b00db8dbc6af34c7c82b060b5570b0b7b24859a7d5547c5a`;
- `activate-preset-command.provisional.json`;
- `preset-payload.provisional.json`, SHA-256
  `c9be5621fa0109c6acbf4461a47191b80b3b6ebae2d50714d5c56172f7cc5174`;
- expected applied, failed and checksum-mismatch event files matching the
  existing serializer tests and the provisional examples supplied by EasyLED;
  do not add fields that are absent from those examples.

These remain test fixtures, not a canonical production contract. Do not add
credentials or use `example.invalid` as a live endpoint.

## Bounded payload adapter

Add a small component API that validates exact config/preset JSON bytes and
produces a complete candidate using fixed-capacity application-owned values:

- at most two ordered physical outputs and 32 segments;
- `segment_index` is unique in 0..31 and maps to Composer stable id
  `segment_index + 1` because Composer reserves id zero for ephemeral segments;
- effect ids are validated through `esp_pixels_fx::describe()`, not a second
  catalog; ids 0..8 remain stable;
- every consumed JSON member rejects duplicate names, wrong types, fractions
  for integer fields and values outside the real application/Composer limits;
- unknown members are accepted for forward compatibility;
- colors use strict `#RRGGBB`; supported blend names map to the existing
  `BlendMode`; omitted effect parameters retain `FxParams` defaults;
- the fixture's `ledmap:null` means identity; `inputs` must currently be an
  empty array;
- output type must be `digital_out`, GPIO/pixel count/order must pass the
  application's real limits, `WS2812B` is accepted, and physical
  `reversed:true` is rejected as unsupported rather than silently ignored;
- the current provisional fixture is 1D. Reject `is_2d:true` and non-null
  ledmaps with a stable unsupported-schema result until EasyLED closes those
  payload fields. Do not invent a canonical 2D schema in this task;
- absent `mirror`, segment `reversed`, `custom[3]` and `color_source` retain
  current firmware defaults. If these unknown fields are present, do not
  silently claim they were applied: return the same unsupported-schema result
  until their EasyLED names/types are closed;
- config payloads carry outputs plus global brightness and a full scene;
  preset payloads carry a replacement scene and must not redefine outputs.

Keep the raw payload size explicitly bounded and documented. Use checked size
arithmetic. Parsing may use the already selected cJSON component; do not place
the maximum candidate or payload buffer on a small FreeRTOS task stack.

## Executor and injected boundaries

Add a synchronous executor called only from the application/control task, never
from an MQTT callback or the render role. Keep interfaces small and boring:

- a payload source resolves config URL/version or preset UUID to exact bytes
  and an expected checksum; the validation fake serves the versioned fixtures;
- an event sink receives serialized bytes for publication and records call
  order in tests;
- a timestamp source supplies each event time;
- an active-target probe can report that the same preset id/checksum is already
  active, allowing the required successful no-op without restarting animation;
- continue using `TerminalJournal` and `DurableStaging`; do not select the final
  NVS journal capacity, eviction policy or HTTPS cache layout here.

For a new valid command, enforce this order:

1. Perform only a bounded JSON/unique-UUID extraction of `command_id`.
2. If a terminal journal record exists, republish those exact stored bytes and
   stop. Do not fully parse/revalidate the envelope, resolve, checksum,
   validate, stage or activate again. Add a narrowly named helper to the
   existing contract component if needed; do not create a second JSON parser.
3. Fully parse the new command envelope, then emit `command_received`.
4. Resolve exact payload bytes, verify SHA-256 where applicable, and validate
   the complete candidate before mutating durable or live state.
5. Emit `command_accepted` only after the command and candidate are acceptable.
6. `begin` -> ordered `append` -> `verifyAndCommit`.
7. Activate at the render frame boundary and confirm the active state matches
   the durable candidate.
8. Serialize and durably store the terminal `command_applied` record, then
   publish those exact bytes.

The first integration supports only `transition="instant"` and
`duration_ms=0`. A new command id requesting the preset already active skips
staging/activation, stores and emits a new successful terminal result, and
does not restart the animation.

Checksum mismatch and unsupported/invalid payload are rejected before staging.
Storage or activation errors produce `command_failed`, call rollback and never
produce `command_applied`. If storing the terminal applied record fails after
activation, rollback to the prior durable/live state and emit failed; an
"applied but not saved" outcome is forbidden. Preserve the original terminal
event bytes, timestamp, reason and message for duplicate replay.

For every terminal outcome with a valid command id, serialize and store the
terminal record before publishing it. A publication enqueue failure does not
undo an already committed/applied command: the durable terminal record remains
available for later replay. If the journal itself cannot store a rejected or
failed result, return the storage error and do not pretend deduplication is
guaranteed. Intermediate received/accepted publication failure must not turn a
valid local transaction into a partially applied state.

`reboot` and `ota` remain parsed by the existing envelope component but are
explicitly rejected as not implemented by this executor. Do not reboot or
start OTA in this task.

Use compact stable provisional reason codes and document them. Reuse
`checksum_mismatch` exactly. Keep diagnostic logs free of full payloads,
credentials and URLs.

## Focused verification

Extend the existing QEMU validation target instead of creating a broad new
test project. Keep this suite focused; do not duplicate the thousands of
Composer/scene tests that already pass.

Required behavior gates:

- exact config and preset fixture parsing into the expected topology,
  brightness, segment geometry, effect and parameters;
- unknown-field acceptance plus a table of representative consumed-field
  type/range/duplicate failures;
- `apply_config` happy-path event and transaction order;
- `activate_preset` cache hit, cache miss and already-active new-command no-op;
- exact checksum mismatch rejection before staging;
- commit, activation and terminal-journal failure rollback, with no applied
  event;
- terminal publication failure after successful durable apply, proving the
  command stays applied and its exact result remains replayable;
- duplicate terminal replay byte-for-byte with zero payload/staging calls;
- a fake durable target that activates the parsed candidate through a real
  Composer/FakeSink and proves the fixture changes the rendered frame;
- no assertion, panic, leak across repeated commands or large object placed on
  the task stack.

Prefer a compact scenario table and fault-injection fake over many nearly
identical tests. Report the number of new scenarios/checks, but do not pursue a
large check count as a goal.

Build the combined `pixels_network_session` application and its validation
target with ESP-IDF 6.0.2. Execute the validation target in QEMU, update its
success marker/count, report application image size plus static DRAM/IRAM, and
run `git diff --check`. Compilation alone is not execution.

## Handoff

Update the network-session README with the exact supported subset, lifecycle,
reason codes, memory limits and remaining provisional gaps. Report touched
files, build/QEMU evidence and concrete limitations. Leave all changes in the
working tree for Codex review.

Do not implement real HTTPS, credentials/bootstrap, MQTT topic policy,
capability publication, final NVS journal/cache policy, OTA or EasyLED backend
changes in this task. Those become separate increments after this executor is
reviewed and the corresponding contracts are closed.
