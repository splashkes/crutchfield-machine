# ADR-0012 — Clean-room pattern engine, not Strudel's AGPL source

**Status:** Accepted
**Date:** 2026-04-20

## Context

With ADR-0010 (QuickJS) we had a JS runtime in-process. The obvious next
step: vendor Strudel's actual npm packages (`@strudel/core`,
`@strudel/mini`, `@strudel/transpiler`) into `js/` and load them. The
pattern layer is pure JS, no browser APIs.

Problem: **every `@strudel/*` package is licensed AGPL-3.0-or-later.**
Bundling any of them into feedback.exe would force feedback as a whole
to AGPL — anyone who runs it has the right to demand full source,
anyone who modifies and distributes it must publish their changes, and
commercial closed-source use is foreclosed.

feedback is MIT. Simon had to pick.

Options Simon was offered:

- **A. Relicense feedback as AGPL.** Clean in the short term but can't
  reasonably be reversed; affects every future contributor.
- **B. Don't bundle — dynamic load / user-install.** Legally grey
  (FSF's position is that dynamic linking still creates a combined
  work). Bad UX (user has to know Strudel exists and install it
  separately).
- **C. Clean-room native implementation.** Fresh JS file matching
  Strudel's public DSL syntax without using Strudel's code. Project
  stays MIT. Accept a compat ceiling.
- **D. Mixed / opt-in Strudel replacement.** Our engine by default;
  user can drop in Strudel to replace it under AGPL. Complicated.

Simon picked C without hesitation. Reasoning: "tight native loop, no
licensing entanglement, still MIT". The long-term ambition of
bidirectional video↔music coupling (visual scalars driving the music,
music triggering visual mutations) depends on not introducing
AGPL'd code paths that would limit who can use or extend the system.

## Decision

Write `js/engine.js` from scratch. Strudel's *syntax* is treated as a
public specification — mini notation, combinator names, chainable
setters. The *implementation* is independent.

Scope shipped in v0.1.3:

- `Pattern` class with `queryArc(begin, end)` → array of Haps.
- Hap structure: `{ whole: {begin, end}, part: {begin, end}, value }`.
- Mini notation: `" "` sequence, `*N` / `/N` speed, `~` rest,
  `[...]` group, `<...>` alternate, `:N` suffix, `,` polyrhythm.
- Combinators: `.fast`, `.slow`, `.rev`, `.every`, `.sometimesBy`,
  `.sometimes`, `.rarely`, `.often`, `.always`, `.never`.
- Value setters: `.s`, `.note`, `.gain`, `.pan`, `.speed`, `.lpf`,
  `.hpf`, `.bpf`, `.delay`, `.room`, `.crush`, `.channel`, `.attack`,
  `.decayT`, `.sustain`, `.release`.
- Top-level: `s()`, `note()`, `stack()`, `cat` (alias slowcat),
  `seq` (alias fastcat), `silence`, `pure`, `parseMini` / `mini`.

Scope **not** shipped (handled by `_UNIMPL_METHODS` safety net — pasted
Strudel code that references these will log a one-time warning and
continue with the method as a no-op that returns `this`):

- `.euclid`, `.euclidLegato`, `.euclidRot` — Euclidean rhythms.
- `.arp`, `.arpWith` — arpeggiators.
- `.jux`, `.juxBy`, `.off`, `.ply`, `.chunk`, `.struct`, `.mask`,
  `.when` — structural transformations.
- `.vowel`, `.shape`, `.coarse`, `.cut`, `.begin`, `.end`, `.loop`,
  `.lpq`, `.hpq`, `.bpq`, `.lpenv`, `.hpenv`, `.phaser`, `.tremolo`,
  `.dry`, `.wet` — audio DSP not implemented.
- `.cps`, `.legato`, `.swing`, `.swingBy`, `.shuffle` — tempo/time
  helpers.

Value-transforming methods that are **NOT** in the no-op list
(`.range`, `.add`, `.sub`, `.zoom`) — these fall through to an
exception because silently mis-mapping a value would be worse than a
crash. User gets a clear error.

## Consequences

**Positive:**
- Project stays MIT. Commercial and closed-source use remain open,
  contributors retain their licensing choices.
- We control the engine. Adding combinators we actually need
  (Euclidean next) is straightforward — a `Pattern.prototype` method
  plus removal from the no-op list.
- Implementation matches the audio engine's capabilities. We don't
  expose pattern features for effects we haven't built.
- Tight feedback loop with the native audio engine is possible
  because our Pattern emits data in the shape our `Event` struct
  expects. No adapter.

**Negative:**
- Strudel compat ceiling. Pasted snippets that use features we
  don't implement will partially work (with logged warnings) or
  visibly fail. This is deliberate — the user was informed and
  accepted the trade in the A/B/C/D conversation.
- When Strudel adds new combinators, we don't get them free. We
  track upstream manually.
- Risk of divergence: if `.fast(2)` behaves subtly differently from
  Strudel's `.fast(2)` (edge cases around rational-time rounding,
  cycle boundaries), pasted snippets can misbehave without
  an obvious error. Tested against simple cases; watch for drift.

## Invariants this ADR locks in

- `js/engine.js` must not be derived from Strudel's source. Future
  contributors: if you want to reference a Strudel behaviour, read
  the *docs*, not the code. Don't copy-paste even snippets.
- Adding a Pattern method means: spec the behaviour against public
  Strudel docs / strudel.cc examples, implement from scratch, add
  a test exercising the cycle query output.
- Any Strudel-compatibility claim in user-facing docs names the
  specific subset we support. Do not claim "runs any Strudel
  snippet" — we don't.

## References

- AGPL-3.0 license text: https://www.gnu.org/licenses/agpl-3.0.html
- Strudel packages on npm (for reference, not consumption):
  `@strudel/core`, `@strudel/mini`, `@strudel/transpiler`.
- Strudel public docs: https://strudel.cc/learn/
- Implementation: `js/engine.js`.
- Commit: `4ac4cc6` (step 2) on the `music` branch.
- Related: ADR-0010 (QuickJS runtime), ADR-0011 (virtual MIDI port).
