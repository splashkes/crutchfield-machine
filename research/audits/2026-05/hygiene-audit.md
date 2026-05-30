# Crutchfield Machine — Hygiene Audit

Repo: `/Users/seanthomasevans/workspace/crutchfield-machine`
Branch: `osc-control`
Date: 2026-05-30
Scope: code hygiene + naming + documentation, prep for a Crutchfield-1984-grounded PR.

68 markdown files surveyed; 16 source files (~13.8K LOC excluding vendor); shaders, ADRs, OSC docs, research dir.

---

## Documentation gaps

### Paper-faithful vocabulary is almost entirely absent from feature docs

Only `CREDITS.md` and `research/PHILOSOPHY.md` use the paper's vocabulary (iterated functional equation, fixed point, limit cycle, chaotic attractor, quasi-attractor, dislocation, pinwheel). The user-facing feature docs that describe the same concepts use engine-only words (DYNAMICS, regime, walk-to-chaos, STABLE/TURBULENT/CHAOTIC).

Counts of paper-vocab tokens (`fixed point`, `limit cycle`, `iterated functional`, `log spiral`, `pinwheel`, `dislocation`, `quasi-attractor`, `chaotic attractor`) in the most-read docs:

| Doc | Engine vocab count | Paper vocab count |
|---|---|---|
| `docs/features/META_CONTROLS.md` | 40 | 0 |
| `docs/features/MATHLAB.md` | 11 | 0 |
| `research/PHILOSOPHY.md` | 7 | 1 |
| `development/DESIGN.md` | 1 | 1 |
| `README.md` | 1 | 0 |
| `CREDITS.md` | 2 | 2 |

The classifier in `docs/features/MATHLAB.md:33-41` invents bespoke regime labels (`STABLE`, `TURBULENT`, `CHAOTIC`, `MARGINAL`, `DIVERGENT`) without ever linking back to Crutchfield's Table II vocabulary (fixed point / limit cycle / chaotic attractor / quasi-attractor with dislocations).

### `docs/features/MATHLAB.md` is the highest-priority stale doc

It is still titled "Mathlab dashboard" (file: `docs/features/MATHLAB.md:1`) and refers to the panel as "Mathlab" 11 times. The panel was renamed to **DYNAMICS** in commits `c80cba2 Mathlab → DYNAMICS: interactive cockpit, not a billboard` and `40f5880 DYNAMICS cockpit is finally clickable`. The on-screen title is now hard-coded `"DYNAMICS"` (`overlay.cpp:851`). Doc and UI disagree.

Also stale inside `MATHLAB.md`:
- `MATHLAB.md:5` says "Mathlab is analysis only ... does NOT edit parameters directly". That's no longer true. `c80cba2` added direct slider edit inside the cockpit (see `overlay.cpp:787 drawMathPanel()` + the `MHIT_SLIDER_*` hit-list at `overlay.h:163-180` and the host drain at `main.cpp:5805` calling `apply_action` with the slider value).
- `MATHLAB.md:7` "Why split: ... Building a parallel editor inside Mathlab was duplicative" — written from the pre-cockpit refactor; now misleading because the cockpit *does* edit.
- `MATHLAB.md:87-89` "Phase portrait (optional future) ... Not yet implemented" — still appears to be true, but worth re-checking before PR.

### Other docs still say "Mathlab"

| File:line | Stale text |
|---|---|
| `docs/README.md:3,30,42,62,107,139` | 6 occurrences of "Mathlab dashboard" / "press M to open Mathlab" |
| `docs/features/META_CONTROLS.md:3,62` | "The Mathlab analytical layer ..."; "Mathlab REGIME badge turns red" |
| `OSC_REFERENCE.md:3,16,77` | "analytical Mathlab dashboard"; link to MATHLAB.md; "Live keys: **M** toggles the Mathlab dashboard" |
| `docs-site/.vitepress/config.mjs:25,51` | Navbar text "Mathlab (analytical view)" |
| `input.h:119-120` | Comments still call it "Mathlab dashboard overlay" / "Mathlab nav" |
| `input.cpp:249-252` | ACTIONS table descriptions still say "Mathlab cursor up" etc. |
| `overlay.h:91,104,163` | Comments use "Mathlab panel" |
| `overlay.cpp:368,492,511,514,828` | 5 comments use "Mathlab" or "drawMathPanel" |
| `main.cpp:3535,5508,5805` | 3 comments still say "Mathlab" |
| `text_render.h:3` | "Used by the Mathlab overlay" |
| `ui_panel.cpp:413,473` | Comments say "DYNAMICS cockpit" — already updated, good reference |

No user-facing doc anywhere describes the "DYNAMICS cockpit" by its new name. The cockpit interactivity (sliders, regime invert button, hit-test handoff with `ui_panel`) is undocumented.

### `docs/features/META_CONTROLS.md` reframe gaps

Uses paper-adjacent ideas without the paper:

- `META_CONTROLS.md:11` — `regime.distance.axis` is described as "one fader: STABLE → CHAOTIC walk". That walk traverses what Crutchfield's Table II calls fixed-point → limit-cycle → quasi-attractor → chaotic-attractor. The doc never says that.
- `META_CONTROLS.md:46-54` — describes piecewise (K_c, decay, noise) path with no link to where the bifurcations happen in the iterated map.
- `META_CONTROLS.md:102` — `theater.failsafe` recovery from DIVERGENT ρ>1.001. ρ as "spectral radius of the linearised iteration" is the paper's framing, but the doc never cites which section.

`META_CONTROLS.md:135` — the math.echo OSC topic `/cma/math/halflife` would benefit from a footnote linking decay coefficient L (Crutchfield) → memory half-life (engine).

### `README.md` is light on the paper

Only mention: `README.md:552` — `research/ has the source papers (Crutchfield 1984, Langton 1990, Turing 1952)`. Doesn't pitch the project as a faithful Crutchfield implementation. CREDITS.md does that work but isn't surfaced from README.

### `launch.sh` comments

Clean. No stale references. Stale-build detection block at `launch.sh:34-48` is well-commented and current.

### Research dir

`research/PHILOSOPHY.md`, `research/README.md`, and `research/precedents/dynamics-as-control-surface.md` are all current. PHILOSOPHY.md is well-grounded (cites Crutchfield 1984 directly, uses iterated functional equation, mentions Vasulka archive). Could carry more weight if linked from README earlier.

### Sections that would benefit from paper-grounded language

1. `docs/features/MATHLAB.md` → rename file to `DYNAMICS.md`; restitle to "DYNAMICS cockpit"; rewrite intro to map the classifier categories onto Crutchfield Table II categories (fixed point / limit cycle / quasi-attractor / chaotic attractor).
2. `docs/features/META_CONTROLS.md` § "1 — `dyn.halflife.axis`" → name-drop Crutchfield's L (storage decay) so the half-life math has a paper anchor.
3. `docs/features/META_CONTROLS.md` § "2 — `regime.distance.axis`" → the path through (K_c, decay, noise) is a one-fader bifurcation walk. Name it that.
4. `README.md` § Background → expand the one-line `research/` pointer into a 3-paragraph "what this is" paragraph that pitches the project as a Crutchfield-1984 implementation.
5. `development/LAYERS.md` § "Camera-side — `invert`, `physics`" → already uses "Crutchfield s parameter" at line 18, good. Could add "this is Crutchfield's luminance inversion (`s = -1` in his eqs. 1, 3)".
6. CREDITS.md is already the gold standard. Cross-link it from every feature doc.

---

## Naming inconsistencies (canonical name table)

The Params struct (`main.cpp:323-430`) is the canonical store. Most parameters have one host name and one shader uniform name. The drift starts when meta-controls and OSC addresses bring in new vocab.

### Paper variable → all names found

| Paper symbol | Crutchfield meaning | Names in codebase | Canonical (recommend) | Rename needed |
|---|---|---|---|---|
| `L` | storage decay (multiplicative per-frame attenuation) | `p.decay` (`main.cpp:338`), `uDecay` (`shaders/main.frag:33`), `effDecay` (`main.cpp:4640`), "memory" in HUD (`docs/features/MATHLAB.md:65`, `META_CONTROLS.md:3`), `halflife_sec` (`main.cpp:3349`, `overlay.cpp:857`), "memory half-life" (overlay UI), `/cma/decay` OSC (`docs/osc/BINDINGS.md:122`), `/cma/dyn/halflife` OSC (`META_CONTROLS.md:26`) | **`decay`** for the raw coefficient; **`memoryHalflife`** when expressed in seconds | None — but doc the L↔decay↔halflife identity in one place |
| `f` (camera intake) | external camera mix into the loop | `p.external` (`main.cpp:347`), `uExternal` (`shaders/main.frag:62`), `dyn.external+/-` actions (`input.cpp:171-172`), `dyn.external.axis` (`input.cpp:313`), `L_EXTERNAL` layer bit, `/cma/external` OSC, `layer.external` action, ACT_EXTERNAL_AXIS comment says "external/camera blend" (`input.h:177`) | **`external`** — already consistent | None |
| `s` (luminance inversion) | sign flip on incoming luminance | `p.invert` (`main.cpp:369`), `uInvert` (`shaders/main.frag:48`), `phys.invert` action (`input.cpp:175`), `ACT_INVERT_TOGGLE`, `regime.invert` (different action!, `input.cpp:257`), `bpm.invert*` (`input.cpp:332-334`), `MHIT_BUTTON_INVERT` (`overlay.h:172`), CREDITS.md uses `s = -1` (Crutchfield's notation) | **`invert`** for the layer; **`regimeFlip`** would be a less ambiguous name for `ACT_REGIME_INVERT` (it doesn't invert anything, it crosses a bifurcation boundary) | Consider renaming `regime.invert` → `regime.flip` or `regime.bifurcate` to disambiguate from `phys.invert` |
| `b` (zoom) | geometric magnification | `p.zoom` (`main.cpp:325`), `uZoom` (`shaders/main.frag:21`), `warp.zoom+/-`, `warp.zoom.axis`, `/cma/zoom` OSC, CREDITS.md maps `b → Params::zoom` (`CREDITS.md:22`) | **`zoom`** | None |
| `φ` (rotation) | warp rotation angle | `p.theta` (`main.cpp:325`), `uTheta` (`shaders/main.frag:21`), `warp.theta+/-`, `warp.theta.axis`, CREDITS.md maps `φ → Params::theta` (`CREDITS.md:23`) | **`theta`** | None — but CREDITS table is the only place φ is named; worth a comment in `Params` |
| `K_c` (coupling) | cross-field coupling strength (Kaneko CML) | `p.couple` (`main.cpp:345`), `uCouple` (`shaders/main.frag:60`), `dyn.couple+/-` actions, `dyn.couple.axis`, `K_c` only in `docs/features/MATHLAB.md:28`, `META_CONTROLS.md:53` (not in code, not in CREDITS table) | **`couple`** in code; **`K_c`** in docs (with one-line "K_c is the couple parameter from Kaneko CML" footnote) | None — but add the Kaneko Kc identity to CREDITS.md |
| `a` (diffusion) | spatial diffusion = blur | `p.blurX`, `p.blurY`, `p.blurAngle` (`main.cpp:330`), `uBlurX/Y/Angle`, `optics.blurX+/-`, "diffusion D" only in `MATHLAB.md:28` (computed as `0.5×(σx²+σy²)×0.5`), CREDITS.md maps `a → blurX/Y Gaussian kernel` (`CREDITS.md:29`) | **`blur{X,Y,Angle}`** in code; **`σ` / `diffusion D`** in derived math display | None |
| `noise` (sensor floor) | stochastic forcing per frame | `p.noise` (`main.cpp:343`), `uNoise` (`shaders/main.frag:38`), `dyn.noise+/-`, `q.noise` (different — quality cycle), `noise floor (dB)` in MATHLAB derivation, `/cma/noise` OSC, CREDITS.md maps "noise floor (Appendix)" → `Params::noise` (`CREDITS.md:30`) | **`noise`** for amplitude; **`noiseQuality`** for the archetype cycle (already named that internally) | None — but `q.noise` vs `dyn.noise+` is a footgun for first-time users; consider renaming the cycle action to `q.noiseType` |

### Other naming drift worth flagging

- **`hueRate` vs `hue` vs `hueBeatKick`** — three different things, three different names, none of them in CREDITS. `p.hueRate` (per-frame radians), `hueJumpStep` (degrees-per-beat at sync time), `hueBeatKick` (one-frame additive). All correct, but a comment block in `Params` would help.
- **`fxWet` vs `sourceWet`** — newer additions (`main.cpp:349-352`), no doc anywhere. They control "dry/wet" mix into / out of the effect chain. Not in CREDITS, not in README, not in feature docs.
- **`flashDecay`, `decayDipTimer`** — Crutchfield-side these are not paper concepts; they're beat-driven transients. Comments at `main.cpp:426-429` are clear. Worth a one-line "engine-only, not in paper" tag in CREDITS so the reader doesn't go looking for them.
- **The classifier categories themselves** — STABLE / TURBULENT / CHAOTIC / MARGINAL / DIVERGENT (`docs/features/MATHLAB.md:36-41`) overlap inconsistently with Crutchfield Table II (fixed point, limit cycle, quasi-attractor with dislocations, chaotic attractor). Recommendation: keep the engine names in the UI (they're shorter) but add a doc table mapping them to the paper terms.

---

## Dead / questionable / redundant code (with file:line)

### Documentation/comment debt from the Mathlab → DYNAMICS rename

Already enumerated above; these are technically code changes too:

- `input.h:119-120` — Comments call it "Mathlab dashboard overlay" and "Mathlab nav". `ACT_MATH_TOGGLE` enum entry could stay (action names are stable), but the comment should say "toggle the DYNAMICS cockpit overlay".
- `input.cpp:249-252` — ACTIONS table description strings say "Mathlab cursor up/down/dec/inc". These end up in help panel, OSC catalogue and (potentially) bindings.ini comments.
- `overlay.cpp:368,492,511,514,828` — Five comments use "Mathlab" or "drawMathPanel".
- `overlay.cpp:787` — Function is still named `drawMathPanel()`. Renaming to `drawDynamicsPanel()` is a mechanical refactor; touches `overlay.h:203` and the `if (mathVisible_) drawMathPanel();` call site at `overlay.cpp:331`. Worth doing pre-PR.
- `overlay.h:91,104,163,169,172,203` — Same family of comments + the `mathVisible_`, `mathPushFrame`, `MHIT_*` symbols. Renaming is more invasive than just MATHLAB.md but the public API surface is small.
- `text_render.h:3` — "Used by the Mathlab overlay and the on-screen HUD" — just update the comment.
- `main.cpp:3535,5508,5805` — comments.

### Submodule trap: `vendor/syphon` and `vendor/link` have no `.gitmodules`

`git ls-tree HEAD vendor/` shows two `commit` entries (`vendor/link`, `vendor/syphon`) but there is no `.gitmodules` file at repo root (`cat .gitmodules` → "No such file or directory"). `git submodule status` reports `fatal: no submodule mapping found in .gitmodules for path 'vendor/link'`.

Effect: a fresh clone of this repo will get empty `vendor/link/` and `vendor/syphon/` directories. The Syphon build (`make SYPHON=1`) will fail; the Ableton Link build will fail. The local working copy has full source checked out because the directories were populated manually (and `vendor/syphon` has untracked content showing in git status).

This is a hard blocker for any PR that asks a third party to build the macOS branch.

Three options:

1. Add a `.gitmodules` mapping both directories to their upstream remotes (preferred — preserves submodule contract).
2. Vendor the source directly (delete .git inside each, commit as plain files).
3. Document a manual fetch step in `development/RUNBOOK.md` (least good — breaks CI).

`vendor/syphon` also shows "modified: vendor/syphon (untracked content)" which suggests local edits or build artefacts inside the submodule that aren't on its upstream branch. Worth investigating before any submodule .gitmodules fix.

### Duplicated uniform-binding code in `render_field` and `render_volume_field`

`main.cpp:4591-4699` (`render_field`) and `main.cpp:4700-4807` (`render_volume_field`) bind the *same* ~70 uniforms to the *same* values from `S.p`. The volume version diverges only in:

- 5 lines of texture binding (binds `flatFallback.tex` to TEXTURE0/1, fills TEXTURE3/4 with the volume ping-pong instead).
- Viewport (`dst.size, dst.size` vs `dst.w, dst.h`).
- `uVolumeSize` set to runtime size vs static `S.volumeSize`.
- The trailing `for (int z = 0; z < dst.size; z++)` slice loop that calls `glFramebufferTextureLayer` per z-slice and re-issues `glDrawArrays`.

Concretely, lines `main.cpp:4625-4695` and `main.cpp:4734-4804` are line-for-line identical uniform-setting code. ~70 lines of duplicate. Bug risk: adding a new uniform requires updating both. Refactor: extract `bind_feedback_uniforms(int fieldId)` helper that both call.

### Untracked working-tree state (per git ls-files --others)

Nothing showed in the audit — `git ls-files --others --exclude-standard` returned empty. The only "untracked" item flagged in `git status` is the submodule content drift.

### Comments referencing removed code (none found)

- `overlay.cpp:368` — "Panel background removed — each text line now gets its own tight" — this is a legitimate explanation of current behaviour, not a stale "X was here". Fine.
- `input.cpp:2963` — "be edited or removed independently of the OSC ingestion config" — context-current.
- `camera_avfoundation.mm:142` — "On older releases we fall back to the deprecated" — flagging a real deprecation, fine.
- No "old layout" / "was here" / "removed in refactor" stragglers found in project code.

### Debug prints

No stray "[debug]" prints in project source. The only opt-in debug logger is `FEEDBACK_MUSIC_DEBUG` env var (`music.cpp:323-344`), which is intentional and well-gated. All other `fprintf(stderr, ...)` are real error reporting (camera negotiation failures, shader compile errors, JS exceptions, MIDI bindings parser warnings) and should stay.

### Vendor TODO/FIXME comments

All TODO/FIXME hits are in `vendor/miniaudio.h`, `vendor/stb_truetype.h`, `vendor/quickjs/*`, `vendor/syphon/*` — third-party upstream code. Zero TODO/FIXME in project source files. Nothing to clean.

### Unused shader uniforms

Cross-check of `grep "^uniform " shaders/` vs `glGetUniformLocation` / `glUniform*` call sites in main.cpp + ui_panel.cpp: **no uniforms declared in shaders are unset from host**. Good hygiene already.

### Dead actions (declared but never dispatched)

Cross-check of `ACT_*` identifiers in `input.h` vs `case ACT_*` / `id == ACT_*` matches in `main.cpp::apply_action`:

- Only `ACT_MACRO_BASE` is declared without a switch case. This is intentional — it's a base sentinel that macro registration adds to (`input.cpp:352-353`), not a real action. Safe.

### Actions in ACTIONS table with no default binding (potentially stale)

The set of actions defined in `input.cpp::ACTIONS[]` but never assigned a default key/gamepad binding in `Input::installDefaults` (lines 624-1100):

- All `ACT_DYN_*`, `ACT_REGIME_*`, `ACT_PAD_*`, `ACT_MATH_*`, `ACT_THEATER_*`, `ACT_LINK_*`, `ACT_SNAPSHOT_*`, `ACT_NOISEQ_*` (white/pink/grain/scanline), `ACT_LAYER_CURSOR_*`, `ACT_QUALITY_CURSOR_*` — all intentionally OSC/MIDI-only or live in higher-level help nav. These are documented in OSC + META_CONTROLS docs.

No dead action handlers — every binding-less action has a real OSC use case.

### Stale build artefacts committed?

`git ls-files | grep -E "\.(o|exe)$"` → empty. Good. The `.o` files in the working dir (`main.o`, `audio.o`, etc.) are .gitignored. The `feedback` binary in repo root is also .gitignored.

### Old PDFs in repo (not a hygiene issue)

`research/Crutchfield_1984_Vasulka.pdf` (tracked, 19 pages), `research/Langton_1990_ComputationEdgeOfChaos.pdf`, `research/Turing_1952_ChemicalBasisOfMorphogenesis.pdf`. All intentional, referenced by research/README.md and CREDITS.md. Keep.

### Pinned UI controls hardcoded

`ui_panel.cpp:135` — `pinned_ = {"decay", "external", "sphereMode", "sphereReverb", "outFade"};` and `ui_panel.cpp:148` lists default control order. These are reasonable defaults but un-doc'd. Worth a one-line comment about how to add a new pin default.

---

## Cleanup priority

### Must-do for PR (blockers)

1. **Fix vendor submodules.** Add a `.gitmodules` mapping `vendor/link` → Ableton/link upstream commit `902aef95bf94af49746fdda5369b42cdcfa1e6d2` and `vendor/syphon` → Syphon-Framework upstream commit `71351d4b484cd2d1917867f7846a5cdca724552d`. Verify `git clone --recurse-submodules` produces a buildable tree. Also clarify the "modified: vendor/syphon (untracked content)" state — either commit the upstream PR or discard the local diff.
2. **Rename `docs/features/MATHLAB.md` to `DYNAMICS.md`.** Update all incoming links: `docs/README.md:30,42,107`, `OSC_REFERENCE.md:16`, `docs-site/.vitepress/config.mjs:25,51`. Rewrite the file intro so the doc matches what the cockpit actually does (it edits, it doesn't just analyse). Map the classifier regimes to Crutchfield Table II categories.
3. **Update the 13 docs and 30+ source comments still saying "Mathlab".** A find-and-replace of "Mathlab" → "DYNAMICS cockpit" with a manual check on each hit. Locations: `docs/README.md` (6), `OSC_REFERENCE.md` (3), `docs/features/META_CONTROLS.md` (2), `docs/features/SYPHON.md` (1 — verify), and source comments listed in the dead-code section above.
4. **Add a paper-vocabulary section to the new DYNAMICS.md.** One table: classifier label → Crutchfield Table II category → paper section reference. Same for META_CONTROLS.md.
5. **Verify the project still builds without `.git/modules/syphon` and `.git/modules/link`.** A reviewer cloning fresh will hit this immediately.

### Nice-to-have for PR

6. **Refactor the duplicated uniform-binding code** in `render_field` (`main.cpp:4591`) and `render_volume_field` (`main.cpp:4700`). Extract a `bind_feedback_uniforms(int fieldId)` helper. Net change: ~140 lines down to ~80, no functional change, eliminates the "forgot to add new uniform to both paths" failure mode.
7. **Rename `overlay.cpp::drawMathPanel()` → `drawDynamicsPanel()`** and the `mathVisible_` / `mathPushFrame` / `MHIT_*` family. Cosmetic but means future readers don't have to mentally translate.
8. **Cross-link CREDITS.md from every feature doc.** One-line "see CREDITS.md for the paper mapping" at the top of MATHLAB→DYNAMICS.md, META_CONTROLS.md, LAYERS.md. Currently CREDITS.md is the only doc that grounds engine vocab in Crutchfield's variables and it's effectively orphaned.
9. **Pitch the project as Crutchfield 1984 in README.md.** The current Background section is one line at `README.md:552`. A three-paragraph intro that names the paper, summarises what it claims, and pitches this codebase as the first faithful real-time GPU implementation would dramatically raise the project's standing for the audiences who care.
10. **Rename `ACT_REGIME_INVERT` → `ACT_REGIME_FLIP` (or `ACT_REGIME_BIFURCATE`).** Right now `phys.invert` (luminance inversion = Crutchfield's `s`) and `regime.invert` (cross nearest bifurcation boundary) share a verb that means two different things. Bindings.ini compatibility means keeping the OSC alias `/cma/regime/invert` for one release.

### Future (not blocking PR)

11. **Phase-portrait inset.** Promised at `docs/features/MATHLAB.md:87-89` as "future work". The research brief at `research/precedents/dynamics-as-control-surface.md:105-108` makes a strong case it's the single most valuable add. Out of scope for a hygiene PR.
12. **`q.noise` vs `dyn.noise+` action name collision.** A separate rename pass to `q.noiseType` would help OSC newcomers. Compatibility cost: nonzero.
13. **`fxWet` / `sourceWet` docs.** Newer additions with no doc anywhere. Worth a short META_CONTROLS-adjacent doc page once the cockpit rename settles.
14. **Add `K_c` / `L` / `a` / `s` paper-symbol comments to `Params` struct** (`main.cpp:323-430`). Currently only `invert` mentions its paper origin ("Crutchfield's `s`", line 369). Five extra one-line comments would make the code self-documenting against the paper.
15. **Move the Mathlab-era `MathSample`, `mathPushFrame`, `ACT_MATH_*` symbol names through a deprecation cycle**, keep aliases for one release, then drop. Only worth doing if the new DYNAMICS API is more than cosmetic.
