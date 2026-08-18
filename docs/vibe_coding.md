# AI-Assisted Work Brief

This is the compact briefing model for AI-assisted work. It does not repeat
project rules: `AGENTS.md` is authoritative and `docs/rag-guide.md` routes the
task-specific context.

## Minimum Useful Brief

```text
Task:
- What should be created, changed, reviewed, or diagnosed?

Scope:
- Application, core, AT sidecar, external esp_* library, or documentation?
- Which files/components are in scope?

Expected behavior:
- What observable result defines success?
- What errors and boundary cases matter?

Constraints:
- Public API compatibility?
- Memory, concurrency, timing, or allocation limits?
- Files or behavior that must not change?

Hardware (only when relevant):
- Target, peripherals, pins, and transport.

Verification:
- Build target, automated tests, hardware checks, or review criteria.
```

## Additional Context by Task

- **New `esp_*` library:** name, purpose, transport, core dependencies,
  ownership model, and whether manual/managed/AT modes are required.
- **Application:** target behavior and board-specific wiring.
- **Refactor:** behavior/API that must remain unchanged.
- **Review:** desired depth and whether findings should be fixed or only
  reported.

Do not copy the repository's architecture rules into every prompt. Point the
agent to `AGENTS.md`, provide the task-specific facts above, and let the local
documents supply the stable context.
