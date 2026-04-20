# development/ — canonical working docs

**You are an agent (human or AI) about to touch this project. Read this page first.**

These documents are the *single source of truth* for what the system is,
how it's built, what decisions have been made, and what should happen next.
User-facing docs (the root `README.md`, `CONTRIBUTING.md`) exist alongside
these and are for visitors — they summarise; these are the working material.

## What's here

| Doc | Purpose | Update when |
|---|---|---|
| [DESIGN.md](DESIGN.md) | Philosophy, principles, what this IS and IS NOT, target users, aesthetic direction | The *intent* of the system changes. Not often. |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Components, data flow, file map, performance budget, where features live | A component is added, removed, or significantly restructured. |
| [LAYERS.md](LAYERS.md) | Shader pipeline order, per-layer reference, hard-vs-soft reorder constraints, float-precision invariant | A layer is added/removed/reordered, or a pipeline invariant changes. |
| [ADR/](ADR/README.md) | Architecture Decision Records — one file per non-trivial technical decision, with context and consequences | A decision is made that future-you will need to justify. Append-only. |
| [RUNBOOK.md](RUNBOOK.md) | Exact commands for build, test, package, release; troubleshooting | A build step, release procedure, or known-problem workaround changes. |
| [TODO.md](TODO.md) | Prioritized backlog with enough context per item to pick up cold | Work is added, claimed, completed, or re-prioritized. |

## Reading order for a cold pickup

1. **DESIGN.md** — understand what we're making and why. 5 min.
2. **ARCHITECTURE.md** — understand how it's put together. 10 min.
3. **TODO.md** — find something to work on. 5 min.
4. **ADR/** — if the item you're working on touches a prior decision,
   read the relevant ADR(s) first. Ignore otherwise.
5. **RUNBOOK.md** — when you're ready to build, test, or release.

## Format rules

These exist so the docs stay coherent over time. Break them only with reason.

### Single source of truth
One fact lives in one doc. Don't copy the static-linking recipe into
three places. If you need to reference it, link to the canonical spot.
If you find duplicates, consolidate during your session.

### DESIGN describes intent. ARCHITECTURE describes mechanism.
If you're explaining *why we use shader layers*, that's DESIGN. If
you're explaining *how layers are loaded and dispatched*, that's
ARCHITECTURE.

### ADRs are immutable once committed.
If a decision is wrong, write a new ADR that *supersedes* the old one.
Never edit an accepted ADR in place. History is part of the value.

### TODO entries must be pickup-ready.
Each item has:
- **Title** — imperative ("Fix EXR recorder exit hang").
- **Why** — the motivation. A TODO without a reason gets pruned.
- **Where** — specific files, line numbers if relevant.
- **Done when** — acceptance criteria. How will you know it's finished?
- **Estimated effort** — tiny / small / medium / large.

### Describe *current* state, not history.
Except for ADRs, these are not chronicles. If something changed, update
the doc to match reality. Old behavior lives in git history and ADRs.

### Keep it tight.
If a doc is getting long, split it. Nobody reads a 2000-line design doc.
Aim for: DESIGN and RUNBOOK each under ~300 lines, ARCHITECTURE under
~400, TODO as long as needed but grouped by priority and trimmed when
items are done.

### When you finish a task
1. Update TODO.md (mark done or delete, depending on whether the history
   matters).
2. If your work implemented a decision, add an ADR.
3. If your work changed build/release/troubleshooting, update RUNBOOK.
4. If your work restructured a component, update ARCHITECTURE.
5. If your work shifted the *intent* of the project, update DESIGN
   (rare — pause and discuss with a human first).

## Workflow for agents

### Human contributor
- Read top to bottom on first exposure.
- Keep TODO.md open during work; tick items as you go.
- Write an ADR as soon as you make a non-obvious technical call.
- When in doubt, match existing style in the codebase.

### AI agent (Claude, etc.)
- Your session context is lossy. These docs exist because you will
  forget what we discussed. Write findings down, don't rely on memory.
- If you pick up a TODO item, update its status or remove it when done.
- Prefer appending to existing docs over creating new ones. The set of
  docs here is deliberately finite.
- If you're unsure whether something belongs in DESIGN vs ARCHITECTURE
  vs ADR, **put it in the ADR** and let a human promote it later if
  it's load-bearing.
- Session-specific notes go in a dated file under the project root
  (e.g. `WORKLOG.md`), not in here.

## What doesn't belong here

- Session-specific notes or chat logs — those go in `WORKLOG.md` at root.
- Code — obviously. Code is the implementation; docs are the scaffolding.
- User-facing documentation — that's `README.md` and `CONTRIBUTING.md`
  at the root.
- Raw research material — that's `research/`.

## Discoverability

Root `README.md` has a "Developer docs" link pointing here.
`CONTRIBUTING.md` routes contributors here after the shader-authoring
walkthrough. If you add a new doc type, add it to the table above and
update the routing.
