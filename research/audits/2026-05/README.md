# Audit reports · May 2026

Five audits run during the work that landed on the
`sean-evans/dynamics-and-controls` branch. Cross-reference for anyone
reviewing the PR or considering follow-up work.

| File | What it covers |
| --- | --- |
| `shader-audit.md` | Engine shader stack vs Crutchfield 1984 equations 1, 3, 4, 7. Per-uniform paper-symbol mapping. Refactor proposals R1-R7. |
| `cockpit-audit.md` | DYNAMICS cockpit classifier math, regime label fidelity vs Table II, named-phenomena gap analysis, refactor proposals 1-12. |
| `hygiene-audit.md` | Documentation coverage, naming consistency, dead/redundant code, cleanup priority. |
| `feature-doc-status.md` | Per-feature doc coverage table: Link, OSC echo, Syphon, MP4 recorder, Camera, DYNAMICS, OSC bindings, EXR recorder, snapshots, math echo. |
| `smoke-tests.md` | Functional smoke-test results for the same ten features. Pass / fail / skipped + reason. |

The Phase A and Phase B commits on this branch act on the highest-priority
refactor items from these reports. Phase C through D items are open
follow-ups · see `CONTRIBUTING-NOTES-2026-05.md` for the deferred-work
list.

The audits are written as standalone artifacts so a reader can land on
any one of them without needing the others.
