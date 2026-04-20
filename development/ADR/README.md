# Architecture Decision Records

Each ADR is one technical decision, captured at the time it was made
(or retroactively, marked as such). The point is: future-you, or a new
contributor, can reconstruct *why* the code looks the way it does
without hunting through git history or asking.

## Format

Every ADR is a single markdown file following this template:

```markdown
# ADR-NNNN — <decision in a sentence>

**Status:** Accepted | Superseded by ADR-XXXX | Rejected
**Date:** YYYY-MM-DD
**Retroactive:** yes/no  (was this written after the decision was implemented?)

## Context

What forced a decision? What constraints, what tradeoffs were on the table?

## Decision

What we chose. Stated plainly, in present tense.

## Consequences

What does this choice buy us? What does it cost? What becomes harder,
what becomes easier? Include the things we gave up, not just the wins.

## Alternatives considered

What else was on the table, and why we didn't pick it. Keep it honest —
if we haven't actually considered alternatives, say so.

## References

Links to relevant code, PRs, discussions. Files and line numbers rot;
mention filenames/symbol names that'll survive refactors.
```

## Rules

1. **Numbered sequentially, zero-padded to 4 digits** (`0001`, `0002`, …).
2. **One decision per file.** If you're tempted to write "ADR-0007 and
   0008", write two files instead.
3. **Immutable once accepted.** Don't edit an ADR after it's merged.
   If the decision changes, write a new one that supersedes the old
   (update the old's Status line to link to the new one — that's the
   one exception to the immutability rule).
4. **Title is the decision**, not the topic. "Use static linking for
   distribution," not "Linking strategy."
5. **Present tense for the Decision section.** It's what we're doing
   now; past tense is for the Context.
6. **Don't write ADRs for trivial choices.** Variable names, function
   placement, which sort algorithm — these belong in code review, not
   here. ADRs are for choices future-you will need to justify.

## When to write one

Write an ADR when a choice is non-obvious and will *shape* later work.
Good triggers:

- A performance-driven choice that looks weird without context
  (e.g. "why do we use PBOs instead of glMapBuffer directly?").
- A tradeoff between two reasonable options where we picked one.
- A dependency you're taking on (a library, a platform assumption).
- A constraint you're accepting (e.g. "we target GL 4.6, not GL ES").
- Something you know will get questioned ("why is this in its own
  thread?").

**Bad triggers:** naming decisions, minor refactors, bug fixes.
Use git commits for those — the history is their ADR.

## Index

| # | Title | Status | Date |
|---|---|---|---|
| [0001](0001-rgba32f-default-precision.md) | RGBA32F feedback buffer as default precision | Accepted | 2026-04-19 (retroactive) |
| [0002](0002-half-float-exr-recording.md) | Half-float RGBA EXR as the archival recording format | Accepted | 2026-04-19 (retroactive) |
| [0003](0003-static-linking-for-distribution.md) | Static-link everything MSYS2-provided for the Windows binary | Accepted | 2026-04-19 |
| [0004](0004-shader-layer-system.md) | One `.glsl` file per layer, dispatched via bitfield from `main.frag` | Accepted | 2026-04-19 (retroactive) |
| [0005](0005-three-stage-recorder-pipeline.md) | Three-stage recorder: capture → reader pool → encoder pool with shared RAM buffer | Accepted | 2026-04-19 |
| [0006](0006-console-picker-on-zero-args.md) | Show interactive console picker when launched with no CLI args | Accepted | 2026-04-19 |
| [0007](0007-action-registry-and-bindings-ini.md) | Action registry + `bindings.ini` as the unified input layer | Accepted | 2026-04-19 |
| [0008](0008-edirol-v4-single-bus-integration.md) | Edirol V-4 as single-bus effect slots, not a full A/B mixer port | Accepted | 2026-04-19 |
| [0009](0009-contextual-gamepad-per-help-section.md) | Gamepad controls rebind per help section; menu navigation is the only reserved surface | Accepted | 2026-04-19 |
