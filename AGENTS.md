# AGENTS.md — Rules of Engagement for the AI Coding Agent

Read this before writing any code. It governs how you (the coding agent) work in this repository.
Also mirror this file as `.cursorrules` if the tooling prefers that name.

## Prime directives

1. **Correctness over speed.** This is a diff tool for hardware; a subtly wrong result is worse than
   no tool. Never sacrifice correctness to "make it work."
2. **Follow the specs.** `docs/00`–`docs/07` are authoritative. If code and spec disagree, the spec
   wins — or you flag the conflict and stop for human input. Do not silently invent behavior.
3. **One task at a time.** Implement the specific `T#.#` task from `docs/05_BUILD_PLAN.md` you were
   given. Do not jump ahead to later phases. Do not refactor unrelated code.
4. **Tests are part of the task.** No task is done until its acceptance criteria (**[AC]**) pass with
   real, non-trivial tests. Write tests alongside or before the implementation.

## Hard rules

- **The C++ core is the single source of truth.** Connectivity and diff logic live only in
  `libnetdiff`. Never reimplement them in Python, JS, or TS. Surfaces call the library/binary.
- **No platform-locked code on the core path.** No `<windows.h>`, `ShellExecute`, `system()`, or
  OS `#ifdef` in `engine/`. Cross-platform CMake only.
- **Determinism is mandatory.** Sort before emit; never depend on hash-map iteration order.
  Identical input → byte-identical output. If you write code that could vary run-to-run, fix it.
- **No network calls in the CLI, engine, or plugin.** They run fully offline. (Privacy is a feature.)
- **Do not change the authoritative tech-stack choices** in `docs/01` (CMake, C++17, the named
  libraries, etc.) without explicitly flagging it and getting human sign-off.
- **Do not fabricate.** If you don't know how KiCad represents something (buses, hierarchical labels,
  power symbols), say so and ask, or write a failing test and a TODO — do not guess and move on. Wrong
  guesses about the file format are the top risk.
- **Respect the licensing note** (PRD §9): the engine is a fork; do not add code that assumes a
  license that isn't resolved. Flag anything that affects licensing.

## Definition of Done (every task)

- [ ] Implements exactly the assigned task; nothing extra, nothing skipped.
- [ ] All **[AC]** for the task pass.
- [ ] Tests written and passing (unit + the relevant suite in `docs/06`).
- [ ] Builds on Linux/macOS/Windows with `-Wall -Wextra -Werror` (no warnings).
- [ ] Deterministic output verified where applicable.
- [ ] No new core-path platform/network dependencies.
- [ ] Committed with a message referencing the task id (e.g. `feat(diff): T1.4 pin-level changes`).

## When you are blocked or uncertain

- If the spec is ambiguous, state the ambiguity and propose 1–2 options; do **not** pick silently.
- If an input in the corpus breaks your parser, add it as a failing test and report it — that's a
  finding, not a nuisance.
- If achieving an **[AC]** seems to require changing a spec, stop and surface it to the human.

## Style

- C++17, clear names, small functions, typed boundaries between pipeline stages.
- No dead code, no commented-out blocks, no `TODO` without an accompanying issue/test.
- Prefer standard library; justify any new dependency.

## What "commercial grade" means here (so you don't cut corners)

- Handles real multi-sheet designs, not just the happy-path single-sheet demo.
- Never crashes on bad input; every failure has a clear message and a correct exit code.
- Deterministic, tested against KiCad's own netlist, and low-noise on cosmetic changes.
- Cross-platform, installable as a single binary, documented.
Anything less is a prototype, not a product — and the human will reject it.
