# Edirol V-4 — Feature Inventory and Technical Reference

## About this document

This inventory catalogs the controls, parameters, and capabilities of the Edirol V-4 four-channel video mixer (Roland/Edirol, 2003).

**Sourcing rules followed:**
- Every numeric range, parameter name, effect ID, and menu path below is taken verbatim from the factory Owner's Manual (Roland Corporation, © 2003, Model V-4; MIDI Implementation v1.00 dated Dec 13 2002). The manual is referenced as OM throughout.
- Physical/electrical specs are cross-checked between the manual, Roland/Edirol product literature, and independent reviews (Videomaker 2018 archive; SoundandVideoRentals product sheet; B&H Photo Video product page).
- **Completeness rule:** every documented ID or item is listed, even when detailed information is unavailable. Where a source does not provide a description, this is marked explicitly with ⚠ and a brief note about what is missing and where it would reside in the physical manual.
- A closing section ("Sources and confidence notes") summarizes what is verified, what is partially verified, and what is confirmed to exist but is not fully described in available sources.

**Information-availability symbols used:**
- ✔ = fully documented in an available source
- ◐ = exists and is named in the OM menu list, but its individual visual/behavioral characterization is only shown graphically in the printed manual (pages 97–106) and is not available as extracted text
- ⚠ = known to exist, but specific details are missing from available source material

---

## 1. Product identity

| Item | Value |
|---|---|
| Manufacturer | Roland Corporation (Edirol brand) |
| Model | V-4 |
| Class | Standard-definition 4-channel video mixer / switcher / effects processor |
| Release | 2003 |
| Status | Discontinued (successor: V-4EX, HD) |

---

## 2. Physical specifications

| Item | Value | Source |
|---|---|---|
| Dimensions (W × H × D) | 225 × 75 × 290 mm (8⅞ × 3 × 11⁷⁄₁₆ in) | Manufacturer spec sheet |
| Weight | 2.2 kg (4.8 lb) | Manufacturer spec sheet |
| Chassis | Metal | Product literature |
| Power | External 9 V DC adapter (included) | OM |
| Current consumption | ⚠ Referenced in OM "Main specifications" section (p. 83) but exact figure not available in extracted text |

Note: Videomaker's 2018 review listed 9″ W × 10⅔″ D × 4¼″ H and 5 lb. These differ from the manufacturer's published dimensions; the manufacturer figures are used above.

---

## 3. Video signal specifications

| Item | Value |
|---|---|
| Video format | NTSC or PAL (ITU-R BT.601), user-switchable |
| Sampling | 13.5 MHz, 4:2:2 (Y:B-Y:R-Y), 8-bit |
| Internal processing | Fully digital |
| Time-base correction | 2 independent TBCs (one per bus, A and B) |
| Frame synchronization | 2 independent frame synchronizers |
| Resolution | Rated "over 500 lines" per manufacturer |
| Output black-level setup | Switchable 0 IRE or 7.5 IRE |
| Audio | None — the V-4 is video-only |

---

## 4. Connector inventory

### Video inputs
- 4 × composite (RCA), channels 1–4
- 2 × S-Video (Y/C), available on channels 1 and 2 only (shared with composite 1/2 — S-Video takes priority when a plug is inserted)

### Video outputs
- 2 × composite (RCA) — carry the program output
- 1 × S-Video — carries the program output (identical signal to composite outs)
- 1 × composite preview output (RCA) — independent, configurable

### Control
- 1 × MIDI IN (5-pin DIN)
- 1 × MIDI OUT (5-pin DIN, can be switched to THRU mode)

### Power
- 1 × DC IN jack (9 V DC, locking collar on cable)

### Input/output signal levels and impedances
⚠ OM "Main specifications" section (p. 83) lists formal signal levels and impedances. These are not available in the extracted text; consult the printed manual.

---

## 5. Front panel — numbered control inventory

Numbering follows the OM "Front Panel" diagram.

| # | Control | Function |
|---|---|---|
| 12 | Parameter setting buttons (MENU / cursor / ENTER) | Navigate the on-screen menu shown on the preview output |
| 13 | BPM indicator | 3-digit display for tempo and parameter values |
| 14 | Tap button (TAP) | Sets BPM by tap-tempo |
| 15 | Output Fade dial | Bipolar fade toward black (left) / white (right) |
| 16 | Channel A input selector | Assigns one of inputs 1–4 to the A bus |
| 17 | BPM Control dial | Sets tempo or active-effect parameter value |
| 18 | Memory dial (MEMORY) | Selects one of 8 panel-configuration slots |
| 19 | Channel B input selector | Assigns one of inputs 1–4 to the B bus |
| 20 | Channel A effect buttons (4) | Triggers whichever effect is assigned to A1–A4 |
| 21 | Channel A effect control dial (CONTROL) | Adjusts the active A-bus effect's parameter |
| 22 | Channel A Transformer button | Performs the assigned Transformer action on A |
| 23 | Mix button | Selects the Mix (dissolve) transition bank |
| 24 | Wipe button | Selects the Wipe transition bank |
| 25 | EFX button | Selects the EFX (key / slide) transition bank |
| 26 | BPM Sync button | Toggles BPM-synchronized switching |
| 27 | Video fader (T-bar) | Manual cross-fade between A and B buses |
| 28 | Channel B effect buttons (4) | Triggers whichever effect is assigned to B1–B4 |
| 29 | Channel B effect control dial (CONTROL) | Adjusts the active B-bus effect's parameter |
| 30 | Channel B Transformer button | Performs the assigned Transformer action on B |

---

## 6. Rear panel — numbered control inventory

| # | Control | Function |
|---|---|---|
| 1 | Power button (POWER) | Mains on/off |
| 2 | DC IN connector | 9 V DC power input |
| 3 | Power cable hook | Strain-relief hook for the DC lead |
| 4 | MIDI OUT | MIDI output (Out or Thru, software-selectable) |
| 5 | MIDI IN | MIDI input |
| 6 | S-Video OUTPUT | Program S-Video out |
| 7 | S-Video INPUT | S-Video input jacks for channels 1 and 2 |
| 8 | Composite INPUT jacks | RCA inputs for channels 1–4 |
| 9 | Preview Out Select buttons | Assign preview output source |
| 10 | Preview OUT | Preview output (RCA composite) |
| 11 | Composite OUTPUT jacks | Program RCA outputs (×2) |

---

## 7. Signal flow

```
 Inputs 1–4  ──► A-bus input selector ──► A-bus TBC/frame sync ──► A-bus effect ──┐
                                                                                    │
                                                                          Video fader (T-bar)
                                                                                    │
 Inputs 1–4  ──► B-bus input selector ──► B-bus TBC/frame sync ──► B-bus effect ──┘
                                                                                    │
                                                                          Output fade dial
                                                                                    │
                                                      ┌───────────────────┬─────────┴────────┐
                                                      ▼                   ▼                  ▼
                                                Composite out 1     Composite out 2     S-Video out

                                                      Preview selector ──► Preview out (RCA)
```

Key points:
- The three program outputs (2× composite, 1× S-Video) all carry identical signal. No independent routing.
- Preview output is independent and can be set to any of inputs 1–4 or the program output, manually or auto-cycling (see §18).

---

## 8. Memory system

- 8 slots, selected by the front-panel Memory dial.
- **Slot 1** is factory-locked (cannot be edited).
- **Slots 2–8** are user-configurable.

Each slot stores:
- Output Fade dial behavior assignment
- Mix / Wipe / EFX transition assignments
- BPM Sync mode and speed
- Channel A effect buttons 1–4 (which of the 96 effects each triggers)
- Channel B effect buttons 1–4 (likewise)
- Channel A Transformer button behavior
- Channel B Transformer button behavior

Memory Edit operations in the menu:
- `Copy Mem[n]->Mem[m]` — copy one slot's contents to another
- `Copy Mem1->All` — copy slot 1 to all slots
- `Exchange Mem[n]->Mem[m]` — swap two slots

Memory Protect: Off / On (prevents accidental overwrites).

---

## 9. Transition engine — full ID inventory

Transitions are organized into three banks, selected by the Mix / Wipe / EFX buttons. Each bank holds one selected transition per Memory slot. Totals: **1 Mix + 216 Wipes + 4 Key + 27 Slide = 248 transitions.**

### 9.1 Mix bank

| ID | Name | Description | Info |
|---|---|---|---|
| 001 | Mix | Cross-dissolve (fade) between A and B | ✔ |

### 9.2 Wipe bank — Hard-edged wipes (IDs 002–073)

72 distinct hard-edged wipe patterns. The OM identifies each by number only; textual descriptions of individual wipe shapes are not provided in the menu list or main body — each is illustrated graphically in the Transition Effects List diagrams (OM pp. ~97–100). All 72 IDs exist:

IDs 002 (Wipe01) through 073 (Wipe72) — 72 consecutive IDs, each a distinct pattern. ◐ Individual pattern descriptions not available in extracted text.

### 9.3 Wipe bank — Soft-edged wipes (IDs 074–145)

72 patterns, the soft-edge equivalents of the hard-edged wipes.

IDs 074 (SWipe01) through 145 (SWipe72). ◐ Individual pattern descriptions not available in extracted text.

### 9.4 Wipe bank — Multi-border wipes (IDs 146–217)

72 patterns with multiple border transitions.

IDs 146 (MWipe01) through 217 (MWipe72). ◐ Individual pattern descriptions not available in extracted text.

### 9.5 EFX bank — Key transitions (IDs 218–221)

Four key-extraction transitions. From OM "Transition Effects List — Luminance Key":

| ID | Name | Description (from OM) | Info |
|---|---|---|---|
| 218 | Key01 | Dark part of B-channel picture gradually changes to A-channel picture | ✔ (description verified) ⚠ (ID-to-description mapping inferred) |
| 219 | Key02 | Bright part of B-channel picture gradually changes to A-channel picture | ✔ / ⚠ |
| 220 | Key03 | Dark part of A-channel picture gradually changes to B-channel picture | ✔ / ⚠ |
| 221 | Key04 | Bright part of A-channel picture gradually changes to B-channel picture | ✔ / ⚠ |

*Note: The OM presents the four descriptions as a group under "Luminance Key" in the Transition Effects List. The specific ID-to-description mapping above is inferred from the pattern (A↔B symmetry and dark/bright symmetry) but the OM does not print this mapping in a numbered table.*

### 9.6 EFX bank — Slide transitions (IDs 222–248)

27 slide-in/slide-out transitions. The OM groups them into "Normal Type" and "Sequential Type" but does not publish the split point between the two types in extracted text.

| ID | Name | Info |
|---|---|---|
| 222 | Slide01 | ◐ Pattern shown in OM diagrams; type (Normal or Sequential) not specified in extracted text |
| 223 | Slide02 | ◐ |
| 224 | Slide03 | ◐ |
| 225 | Slide04 | ◐ |
| 226 | Slide05 | ◐ |
| 227 | Slide06 | ◐ |
| 228 | Slide07 | ◐ |
| 229 | Slide08 | ◐ |
| 230 | Slide09 | ◐ |
| 231 | Slide10 | ◐ |
| 232 | Slide11 | ◐ |
| 233 | Slide12 | ◐ |
| 234 | Slide13 | ◐ |
| 235 | Slide14 | ◐ |
| 236 | Slide15 | ◐ |
| 237 | Slide16 | ◐ |
| 238 | Slide17 | ◐ |
| 239 | Slide18 | ◐ |
| 240 | Slide19 | ◐ |
| 241 | Slide20 | ◐ |
| 242 | Slide21 | ◐ |
| 243 | Slide22 | ◐ |
| 244 | Slide23 | ◐ |
| 245 | Slide24 | ◐ |
| 246 | Slide25 | ◐ |
| 247 | Slide26 | ◐ |
| 248 | Slide27 | ◐ |

### 9.7 Transition timing

Transition duration is controlled by the BPM Control dial when BPM Sync is off, or locked to tempo when on.

---

## 10. Effects engine — full ID inventory (all 96)

The V-4 contains 96 effects, identified by ID 01–96. Any of the 8 effect buttons (A1–A4, B1–B4) can be assigned any of these IDs. The effect control dial adjusts a single parameter of the currently-lit effect; which parameter varies by effect.

**Naming convention note:** In the OM menu list, the first variant in each family is named with suffix "1" (e.g. `STROBE1`) and subsequent variants appear to be numbered sequentially. The **last** entry in most families is displayed with the family name alone (e.g. `STROBE` at ID 12, without a number). This is taken to be the final numbered variant in that family (e.g. `STROBE10`). For families where both endpoints are numbered (Shake, PinP), the numbering is fully verified. Where the endpoint is unnumbered, intermediate variant names are inferred but individual visual/behavioral characterizations are not published in the OM menu list.

### 10.1 Still (IDs 01–02) — Freeze frame

| ID | Name | Info |
|---|---|---|
| 01 | STILL1 | ✔ Family verified (freeze frame). B&H Photo product copy mentions "field freeze" and "frame freeze" — the two variants are believed to correspond to these two freeze types, but ⚠ this mapping is not confirmed in OM |
| 02 | STILL2 | ✔ / ⚠ Same as above |

### 10.2 Strobe (IDs 03–12) — Intermittent frame hold

10 variants. The parameter controlled by the effect dial is believed to be strobe rate/timing, but the OM does not explicitly describe the dial-controlled parameter for each effect family in extracted text.

| ID | Name | Info |
|---|---|---|
| 03 | STROBE1 | ✔ Verified by OM |
| 04 | STROBE2 | ◐ Implied by sequence; individual characterization not in OM menu list |
| 05 | STROBE3 | ◐ |
| 06 | STROBE4 | ◐ |
| 07 | STROBE5 | ◐ |
| 08 | STROBE6 | ◐ |
| 09 | STROBE7 | ◐ |
| 10 | STROBE8 | ◐ |
| 11 | STROBE9 | ◐ |
| 12 | STROBE (shown as "STROBE" in OM menu; taken to be STROBE10) | ◐ |

### 10.3 Shake (IDs 13–16) — Position-jitter effect

| ID | Name | Info |
|---|---|---|
| 13 | SHAKE1 | ✔ Verified by OM |
| 14 | SHAKE2 | ◐ Implied by sequence |
| 15 | SHAKE3 | ◐ |
| 16 | SHAKE4 | ✔ Verified (endpoint shown in OM as SHAKE4) |

### 10.4 Negative (IDs 17–20) — Inverted brightness and color

| ID | Name | Info |
|---|---|---|
| 17 | NEGATIVE1 | ✔ Verified by OM |
| 18 | NEGATIVE2 | ◐ |
| 19 | NEGATIVE3 | ◐ |
| 20 | NEGATIVE (shown as "NEGATIVE" in OM menu; taken to be NEGATIVE4) | ◐ |

### 10.5 Colorize (IDs 21–29) — Color tint applied to image

9 variants.

| ID | Name | Info |
|---|---|---|
| 21 | COLRIZE1 | ✔ Verified by OM |
| 22 | COLRIZE2 | ◐ |
| 23 | COLRIZE3 | ◐ |
| 24 | COLRIZE4 | ◐ |
| 25 | COLRIZE5 | ◐ |
| 26 | COLRIZE6 | ◐ |
| 27 | COLRIZE7 | ◐ |
| 28 | COLRIZE8 | ◐ |
| 29 | COLRIZE (shown as "COLRIZE" in OM menu; taken to be COLRIZE9) | ◐ |

### 10.6 Monochrome color filter (IDs 30–38) — Desaturate and tint

9 variants.

| ID | Name | Info |
|---|---|---|
| 30 | MONOCOLOR1 | ✔ Verified by OM |
| 31 | MONOCOLOR2 | ◐ |
| 32 | MONOCOLOR3 | ◐ |
| 33 | MONOCOLOR4 | ◐ |
| 34 | MONOCOLOR5 | ◐ |
| 35 | MONOCOLOR6 | ◐ |
| 36 | MONOCOLOR7 | ◐ |
| 37 | MONOCOLOR8 | ◐ |
| 38 | MONOCOLOR (shown as "MONOCOLOR" in OM menu; taken to be MONOCOLOR9) | ◐ |

### 10.7 Posterize (IDs 39–43) — Reduce brightness gradations

5 variants. B&H Photo product copy describes the effect family as "solarize"; terminology may vary, but OM uses "posterize" consistently.

| ID | Name | Info |
|---|---|---|
| 39 | POSTERIZE1 | ✔ Verified by OM |
| 40 | POSTERIZE2 | ◐ |
| 41 | POSTERIZE3 | ◐ |
| 42 | POSTERIZE4 | ◐ |
| 43 | POSTERIZE (shown as "POSTERIZE" in OM menu; taken to be POSTERIZE5) | ◐ |

### 10.8 Color Pass (IDs 44–52) — Monochrome with one color preserved

9 variants. Preserves a selected hue and desaturates all others.

| ID | Name | Info |
|---|---|---|
| 44 | COLORPASS1 | ✔ Verified by OM |
| 45 | COLORPASS2 | ◐ |
| 46 | COLORPASS3 | ◐ |
| 47 | COLORPASS4 | ◐ |
| 48 | COLORPASS5 | ◐ |
| 49 | COLORPASS6 | ◐ |
| 50 | COLORPASS7 | ◐ |
| 51 | COLORPASS8 | ◐ |
| 52 | COLORPASS (shown as "COLORPASS" in OM menu; taken to be COLORPASS9) | ◐ |

### 10.9 White Luminance Key (IDs 53–54) — Extract bright areas

⚠ Both IDs are displayed as `W-LUMIKEY` in the OM menu list, with no numerical suffix on either. Two separate preset slots exist (confirmed by the two distinct IDs); their individual default parameter values are not published in extracted OM text.

| ID | Name | Info |
|---|---|---|
| 53 | W-LUMIKEY (preset 1) | ✔ Exists; ⚠ default parameter values per preset not in extracted OM text |
| 54 | W-LUMIKEY (preset 2) | ✔ Exists; ⚠ same |

### 10.10 Black Luminance Key (IDs 55–56) — Extract dark areas

⚠ Both IDs are displayed as `B-LUMIKEY` in the OM menu list, with no numerical suffix.

| ID | Name | Info |
|---|---|---|
| 55 | B-LUMIKEY (preset 1) | ✔ Exists; ⚠ default parameter values per preset not in extracted OM text |
| 56 | B-LUMIKEY (preset 2) | ✔ Exists; ⚠ same |

### 10.11 Chroma Key (IDs 57–58) — Extract selected color

⚠ Both IDs are displayed as `CHROMAKEY` in the OM menu list, with no numerical suffix.

| ID | Name | Info |
|---|---|---|
| 57 | CHROMAKEY (preset 1) | ✔ Exists; ⚠ default parameter values per preset not in extracted OM text |
| 58 | CHROMAKEY (preset 2) | ✔ Exists; ⚠ same |

### 10.12 Multi-H (IDs 59–63) — Horizontally divided multiscreen

5 variants. Image is repeated in a horizontal tiling pattern.

| ID | Name | Info |
|---|---|---|
| 59 | MULTI-H1 | ✔ Verified by OM |
| 60 | MULTI-H2 | ◐ |
| 61 | MULTI-H3 | ◐ |
| 62 | MULTI-H4 | ◐ |
| 63 | MULTI-H (shown as "MULTI-H" in OM menu; taken to be MULTI-H5) | ◐ |

### 10.13 Multi-V (IDs 64–68) — Vertically divided multiscreen

5 variants.

| ID | Name | Info |
|---|---|---|
| 64 | MULTI-V1 | ✔ Verified by OM |
| 65 | MULTI-V2 | ◐ |
| 66 | MULTI-V3 | ◐ |
| 67 | MULTI-V4 | ◐ |
| 68 | MULTI-V (shown as "MULTI-V" in OM menu; taken to be MULTI-V5) | ◐ |

### 10.14 Multi-HV (IDs 69–73) — Both-axis divided multiscreen

5 variants.

| ID | Name | Info |
|---|---|---|
| 69 | MULTI-HV1 | ✔ Verified by OM |
| 70 | MULTI-HV2 | ◐ |
| 71 | MULTI-HV3 | ◐ |
| 72 | MULTI-HV4 | ◐ |
| 73 | MULTI-HV (shown as "MULTI-HV" in OM menu; taken to be MULTI-HV5) | ◐ |

### 10.15 Mirror-H (IDs 74–78) — Horizontal mirroring

5 variants. Image is flipped on its horizontal axis; variants likely differ in the position of the mirror seam and which half is mirrored, but individual variant descriptions are not published in the OM menu list.

| ID | Name | Info |
|---|---|---|
| 74 | MIRROR-H1 | ✔ Verified by OM |
| 75 | MIRROR-H2 | ◐ Implied by sequence; individual characterization not in OM menu list |
| 76 | MIRROR-H3 | ◐ |
| 77 | MIRROR-H4 | ◐ |
| 78 | MIRROR-H (shown as "MIRROR-H" in OM menu; taken to be MIRROR-H5) | ◐ |

### 10.16 Mirror-V (IDs 79–83) — Vertical mirroring

5 variants.

| ID | Name | Info |
|---|---|---|
| 79 | MIRROR-V1 | ✔ Verified by OM |
| 80 | MIRROR-V2 | ◐ |
| 81 | MIRROR-V3 | ◐ |
| 82 | MIRROR-V4 | ◐ |
| 83 | MIRROR-V (shown as "MIRROR-V" in OM menu; taken to be MIRROR-V5) | ◐ |

### 10.17 Mirror-HV (IDs 84–88) — Both-axis mirroring

5 variants.

| ID | Name | Info |
|---|---|---|
| 84 | MIRROR-HV1 | ✔ Verified by OM |
| 85 | MIRROR-HV2 | ◐ |
| 86 | MIRROR-HV3 | ◐ |
| 87 | MIRROR-HV4 | ◐ |
| 88 | MIRROR-HV (shown as "MIRROR-HV" in OM menu; taken to be MIRROR-HV5) | ◐ |

### 10.18 Picture-in-Picture (IDs 89–96)

8 variants — PinP-1 through PinP-8. Each has a fully independent parameter set of 9 values (see §13).

| ID | Name | Info |
|---|---|---|
| 89 | PinP-1 | ✔ Verified by OM |
| 90 | PinP-2 | ✔ Implied by sequence (OM lists both endpoints) |
| 91 | PinP-3 | ✔ |
| 92 | PinP-4 | ✔ |
| 93 | PinP-5 | ✔ |
| 94 | PinP-6 | ✔ |
| 95 | PinP-7 | ✔ |
| 96 | PinP-8 | ✔ Verified by OM |

### 10.19 Factory-default effect assignments (Memory slot 1)

Confirmed at family level from OM "Changing the Assignments of the Channel A and B Effect Buttons":

| Button | Family | Exact default variant |
|---|---|---|
| Channel A, button 1 | Strobe | ⚠ Not specified in extracted OM text |
| Channel A, button 2 | Negative | ⚠ Not specified |
| Channel A, button 3 | Colorize | ⚠ Not specified |
| Channel A, button 4 | Multi (Multi-H / Multi-V / Multi-HV) | ⚠ Both sub-family and variant not specified |
| Channel B, button 1 | Mirror (Mirror-H / Mirror-V / Mirror-HV) | ⚠ Both sub-family and variant not specified |
| Channel B, button 2 | Chroma Key | ⚠ Preset 57 or 58 not specified |
| Channel B, button 3 | Luminance Key (White or Black) | ⚠ Type and preset not specified |
| Channel B, button 4 | Picture-in-Picture | ⚠ PinP-1 through PinP-8 not specified |

### 10.20 Combinability

OM states that some effects cannot be used simultaneously but does not publish a full conflict matrix as a single table. ⚠ A complete pairwise combinability matrix is not available.

---

## 11. Chroma Key — parameter detail

Menu path: `Key Setup`.

| Parameter | Range | Function |
|---|---|---|
| `ChromaKey Color` | `Blue-Magenta` … `Yellow-Red` (continuous) | Hue of the color to be keyed out |
| `ChromaKey Level` | 000–255 | Extraction threshold (tolerance width around the keyed hue) |
| `ChromaKey Edge` | 001–015 | Edge softness / feathering |

Behavior:
- Keys the B-bus image so that pixels matching `ChromaKey Color` within the `Level` tolerance are replaced by the A-bus image underneath.
- Two preset slots exist (effect IDs 57 and 58) so two independent chroma-key configurations can be held at once.
- During operation, the T-bar controls the reveal of the keyed foreground over the background (full B-bus end = 100% foreground).
- The Level parameter can be swept in real time via the effect control dial when the Chroma Key button is blinking.

⚠ The relationship between the `ChromaKey Color` continuous axis (Blue-Magenta → Yellow-Red) and CIE/HSV color-wheel coordinates is not documented in extracted OM text — specifically, whether the axis is linear in hue angle or weighted toward commonly-used chroma-key colors is not stated.

---

## 12. Luminance Key — parameter detail

Menu path: `Key Setup`.

Two variants with independent parameters:

| Parameter | Range | Function |
|---|---|---|
| `White-LumiKey Level` | 000–255 | Extraction threshold for bright-area keying |
| `White-LumiKey Edge` | 001–015 | Edge softness (white-key) |
| `Black-LumiKey Level` | 000–255 | Extraction threshold for dark-area keying |
| `Black-LumiKey Edge` | 001–015 | Edge softness (black-key) |

Effect IDs:
- White Luminance Key: 53, 54
- Black Luminance Key: 55, 56

Behavior note from the OM: luminance keying fails if the source lacks clear light/dark contrast, and yields inconsistent results on sources without a consistent luminance separation between subject and background.

---

## 13. Picture-in-Picture — parameter detail

Eight fully independent presets (PinP1 through PinP8 — effect IDs 89–96). Each has its own set of nine parameters at menu path `Key Setup → PinP[n] Setup`.

| Parameter | Range | Function |
|---|---|---|
| `HPosi` | 000–153 | Horizontal position of the inset window |
| `VPosi` | 000–207 | Vertical position of the inset window |
| `4:3 HVSize` | 000–110 | Size with locked 4:3 aspect ratio |
| `HSize` | 000–110 | Free horizontal size |
| `VSize` | 000–220 | Free vertical size |
| `Border` | 000–015 | Border width |
| `BColor` | 000–015 | Border color |
| `Shadow` | 000–015 | Drop-shadow distance |
| `SColor` | 000–015 | Drop-shadow color |

⚠ Dimensional units in the OM are not specified as pixels or IRE; the numeric range is what the menu exposes.

⚠ The palette of available Border and Shadow colors (0–15) is not enumerated with named color values in extracted OM text.

---

## 14. Transformer buttons — full behavior table

Each Transformer button (one on each bus) can be assigned any of 36 behaviors, stored per Memory slot. Menu path: `Transformer [Mem 2–8]`.

| ID | Entry | Behavior |
|---|---|---|
| 01 | `none-none` | Button disabled |
| 02 | `Trans-Trans` | Momentary — output the pressed channel while held |
| 03 | `A<->9-9<->B` | Toggle A/B, transition duration 9 (slowest) |
| 04 | `A<->8-8<->B` | Toggle A/B, transition duration 8 |
| 05 | `A<->7-7<->B` | Toggle A/B, transition duration 7 |
| 06 | `A<->6-6<->B` | Toggle A/B, transition duration 6 |
| 07 | `A<->5-5<->B` | Toggle A/B, transition duration 5 |
| 08 | `A<->4-4<->B` | Toggle A/B, transition duration 4 |
| 09 | `A<->3-3<->B` | Toggle A/B, transition duration 3 |
| 10 | `A<->2-2<->B` | Toggle A/B, transition duration 2 |
| 11 | `A<->1-1<->B` | Toggle A/B, transition duration 1 |
| 12 | `A<->0-0<->B` | Toggle A/B, hard cut |
| 13 | `A<-9 - 9->B` | Force output to pressed channel, duration 9 |
| 14 | `A<-8 - 8->B` | Force output to pressed channel, duration 8 |
| 15 | `A<-7 - 7->B` | Force output to pressed channel, duration 7 |
| 16 | `A<-6 - 6->B` | Force output to pressed channel, duration 6 |
| 17 | `A<-5 - 5->B` | Force output to pressed channel, duration 5 |
| 18 | `A<-4 - 4->B` | Force output to pressed channel, duration 4 |
| 19 | `A<-3 - 3->B` | Force output to pressed channel, duration 3 |
| 20 | `A<-2 - 2->B` | Force output to pressed channel, duration 2 |
| 21 | `A<-1 - 1->B` | Force output to pressed channel, duration 1 |
| 22 | `A<-0 - 0->B` | Force output to pressed channel, hard cut |
| 23 | `A->9 - 9<-B` | Force output to opposite channel, duration 9 |
| 24 | `A->8 - 8<-B` | Force output to opposite channel, duration 8 |
| 25 | `A->7 - 7<-B` | Force output to opposite channel, duration 7 |
| 26 | `A->6 - 6<-B` | Force output to opposite channel, duration 6 |
| 27 | `A->5 - 5<-B` | Force output to opposite channel, duration 5 |
| 28 | `A->4 - 4<-B` | Force output to opposite channel, duration 4 |
| 29 | `A->3 - 3<-B` | Force output to opposite channel, duration 3 |
| 30 | `A->2 - 2<-B` | Force output to opposite channel, duration 2 |
| 31 | `A->1 - 1<-B` | Force output to opposite channel, duration 1 |
| 32 | `A->0 - 0<-B` | Force output to opposite channel, hard cut |
| 33 | `White-White` | Fade both channels to white |
| 34 | `Black-Black` | Fade both channels to black |
| 35 | `White-Black` | A to white / B to black |
| 36 | `Black-White` | A to black / B to white |

Digits 0–9 set transition duration, with 9 slowest and 0 a hard cut.

⚠ The absolute time value (in ms or frames) corresponding to each numeric duration 0–9 is not published in extracted OM text. Duration is described only in relative terms.

---

## 15. Output Fade — parameter detail

Menu path: `Output Fade [Mem 2–8]` for mode assignment; `Utility` for level parameters.

### Dial behavior modes

| ID | Mode | Behavior |
|---|---|---|
| 01 | `No Control` | Dial disabled |
| 02 | `Black-White` | Standard bipolar manual fade (left → black, right → white) |
| 03 | `AutoB/W` | Automatic timed fade |

⚠ The timing duration of the `AutoB/W` mode is not published in extracted OM text.

### White and black level trims (Utility menu)

| Parameter | Range | Function |
|---|---|---|
| `OutFade White Level` | 000–255 | Target "white" level when dial is fully right |
| `OutFade Black Level` | 000–255 | Target "black" level when dial is fully left |

---

## 16. BPM Sync — parameter detail

Menu path: `BPM Sync [Mem 2–8]`.

### Mode

| Option | Behavior |
|---|---|
| `Direct A-B` | Hard cut between A and B on each beat |
| `Transition A-B` | Run the currently-selected transition between A and B on each beat |

### Speed

| Option | Behavior |
|---|---|
| `BPMx1/4` | Switch every quarter-beat (quadruple tempo) |
| `BPMx1/2` | Switch every half-beat (double tempo) |
| `BPMx1` | Switch on every beat |
| `BPMx2` | Switch every two beats (half tempo) |

Tempo sources: Tap button on front panel, BPM Control dial, or external MIDI timing clock.

⚠ The BPM range (minimum and maximum tempo values displayable) is not published in extracted OM text.

---

## 17. Video fader (T-bar)

Menu path: `Utility`.

| Parameter | Options | Function |
|---|---|---|
| `Video Fader Mode` | `Normal` / `Quick` | Normal uses full travel; Quick activates only the central range (faster cuts) |
| `Video Fader Curve` | `A` / `B` / `C` | Three selectable taper curves |
| `Video Fader Calibrate A` | — | Calibrates the A-bus end-stop |
| `Video Fader Calibrate B` | — | Calibrates the B-bus end-stop |

⚠ The mathematical shape of curves A, B, C (e.g. linear, square-law, logarithmic) is not published in extracted OM text.

Physical:
- Metal T-bar, quoted at "over 100 steps" of resolution (product literature).
- Mounting orientation is user-selectable — vertical (conventional video-mixer orientation) or horizontal (DJ-mixer orientation).

---

## 18. Utility menu — other settings

Menu path: `Utility`.

### Per-bus proc amp
Independent Brightness / Color / Hue adjusts for the A-bus and B-bus inputs. Numeric ranges as published in OM menu list:

| Parameter | Range | Info |
|---|---|---|
| VideoA Bright Adjust | ⚠ Range unclear in extracted PDF text | Present in menu; numeric endpoints not cleanly extractable |
| VideoA Color Adjust | −60 to +61 | ✔ |
| VideoA Hue Adjust | −10 to +11 | ✔ |
| VideoB Bright Adjust | ⚠ Range unclear in extracted PDF text | Present in menu; numeric endpoints not cleanly extractable |
| VideoB Color Adjust | −60 to +61 | ✔ |
| VideoB Hue Adjust | −10 to +11 | ✔ |

### Video sync and output geometry

| Parameter | Range | Info |
|---|---|---|
| `Video Sync Threshold` | ⚠ Range not cleanly extractable from PDF | Present in OM; adjusts sync detection sensitivity |
| `Video Horizontal Locate` | ⚠ Range not cleanly extractable | Shifts output image horizontally |
| `Video Vertical Locate` | ⚠ Range not cleanly extractable | Shifts output image vertically |
| `Video Display Range H` | ⚠ Range not cleanly extractable | Trims active horizontal area |
| `Video Display Range V` | ⚠ Range not cleanly extractable | Trims active vertical area |
| `Video Out Black Level` | `0 IRE` / `7.5 IRE` | ✔ |

### Other utility controls

| Item | Options |
|---|---|
| `V-4 Mode` | `Normal Mode` / `Presen Mode` |
| `Memory Protect` | Off / On |
| `Input Select Delay Time` | 0–30 (crossfade duration on auto-transition when changing input) |
| `Preview Display Mode` | Off / Mode 1 / Mode 2 / Mode 3 |
| `Preview Switch Pattern` | Manual / Always-1 / Always-2 / Always-3 / Always-4 / Always-Out / Auto 1-2 / Auto 1-3 / Auto 1-4 / Auto 1-Out |
| `Preview Auto Speed` | 0–9 |
| `Preview Signal Check` | Off / On |
| `Preview No Signal Color` | Cyan … Magenta (⚠ interpolated range; intermediate values not enumerated) |
| `No Signal Blueback` | Off / On |
| `OSD Horizontal Locate` | ⚠ Range not cleanly extractable |
| `OSD Vertical Locate` | ⚠ Range not cleanly extractable |
| `Color Bar Out` | Off / On |
| `Factory Preset` | Restore factory defaults |

⚠ The Preview Display Modes 1, 2, and 3 differ from each other in layout, but the differences between the three modes are not described in extracted OM text.

---

## 19. MIDI implementation

Menu path: `MIDI Setup`. All front-panel controls can be mapped to MIDI messages, independently for send and receive.

### Channel / global settings

| Parameter | Range |
|---|---|
| `MIDI Tx Channel` | 1–16 |
| `MIDI Rx Channel` | 1–16 / OFF |
| `MIDI Out/Thru Switch` | `Out` / `Thru` |
| `V-LINK Switch` | Off / On |
| `Note Mode On/Off` | Off / On |
| `Device ID` | 0x00 – 0x1F |

### Per-control MIDI assignment

Each of the following can be assigned to `OFF`, any Control Change number 1–95, Channel Aftertouch, or Pitch Bend:

- Effects-A1 through A4 Assign
- Effects-B1 through B4 Assign
- Video Fader Assign
- Transition Assign
- Transformer-A Assign
- Transformer-B Assign
- BPM/Sync Assign
- Transition Time Assign
- Output Fade Assign

### Note Mode keys
When Note Mode is On, the following MIDI note numbers trigger effects / selections (C2 octave layout):

| Note number | Key name |
|---|---|
| 36 | C2 |
| 38 | D2 |
| 40 | E2 |
| 41 | F2 |
| 43 | G2 |
| 45 | A2 |
| 47 | B2 |
| 48 | C3 |

⚠ Which specific function (which effect button, input, or memory slot) is triggered by each of the eight note numbers is not fully enumerated in extracted OM text.

### Program Change
- Input select: PC values 1–4
- Memory select: PC values 1–8

### MIDI Implementation Chart (v1.00 dated Dec 13 2002)
- Basic Channel Default: 1–16; Changed: 1–16
- Mode: Mode 3 (OMNI OFF, POLY)
- Note number (recognized): 36, 38, 40, 41, 43, 45, 47, 48 — operational only when Note Mode is ON
- Velocity Note-On: recognized only when Presentation Mode is ON
- Aftertouch (Channel): recognized, controls various values
- Pitch Bend: both transmitted and recognized
- Control Change: CC 0, 32 recognized for bank select; CC 1–5, 7–31, 64–95 recognized and transmitted
- Program Change: transmitted and recognized (ranges: 0–3 or 0–7 for input/memory)
- System Exclusive: recognized; used for V-LINK and parameter control
- System Real-Time: Clock, Start, Continue recognized
- Active Sense: recognized

### System-exclusive addressing
OM publishes a Parameter Address Map under two model IDs:
- V-4 native: Model ID `00H 5BH`
- V-LINK: Model ID `00H 51H`

⚠ The complete SysEx address table is printed in OM pp. 92–95 but is not reproduced here; individual parameter addresses are not available in extracted text.

---

## 20. V-LINK

V-LINK is a Roland/Edirol protocol that extends MIDI for synchronized control of video from compatible audio/music devices. On the V-4:

- Enabled/disabled via the `V-LINK Switch` in MIDI Setup.
- Allows another V-LINK device (e.g. Roland MC-909 sequencer, Edirol PCR-30/50 controller keyboard, DV-7PR presenter) to trigger clips, effects, and transitions on the V-4 in real time.
- Uses the dedicated V-LINK model ID (00H 51H) for addressing.

No extra hardware is required — V-LINK rides on the standard MIDI connectors.

⚠ The full list of V-LINK compatible Roland/Edirol devices is not published in the V-4 OM (only examples are given).

---

## 21. Presentation mode

Enabled via `Utility → V-4 Mode → Presen Mode`.

Differences from Normal Mode (documented in OM):
- Memory-dial behavior is repurposed for presentation: the eight slots are pre-mapped to a presentation-oriented set:
  1. Mix
  2. Picture In Picture 1 (PinP1)
  3. Picture In Picture 2 (PinP2)
  4. Picture In Picture 3 (PinP3)
  5. Picture In Picture 4 (PinP4)
  6. White Luminance Key (WLKEY)
  7. Black Luminance Key (BLKEY)
  8. Chroma Key (C-KEY)
- The Channel A input selector auto-applies the selected transition when the user changes source.
- The BPM indicator is repurposed to display the current transition time, adjustable via the BPM Control dial.
- The T-bar controls the level of image B (0–100%) rather than a free A/B crossfade.
- MIDI Note-On velocity becomes recognized (per MIDI Implementation Chart).

---

## 22. Included items (factory box contents)

Per OM:
1. V-4 unit
2. AC adaptor and power cable
3. Owner's manual

⚠ Some regional SKUs may have included additional items (rack-mount hardware was referenced in some listings). This is not confirmed across all regions.

---

## 23. Sources and confidence notes

### Fully verified (✔) — taken directly from available source text
- All effect family names and ID ranges in §10
- First-variant names in each family (e.g. STROBE1, MIRROR-H1, MULTI-V1)
- Last-variant names where shown with numeric suffix (SHAKE4, PinP-8)
- All parameter names and numeric ranges in §11, §12, §13, §15, §16
- Transformer behavior list (all 36 entries) in §14
- Four Key transition descriptions in §9.5 (though exact ID-to-description mapping is inferred)
- MIDI Setup parameters and ranges in §19
- MIDI Implementation Chart summary in §19
- Menu paths and Memory Edit operations in §8
- Presentation Mode memory mapping in §21
- Transition bank ID ranges and totals in §9
- Front and rear panel numbering and functions in §5–§6
- Proc-amp numeric ranges for Color and Hue (per bus) in §18

### Partial / interpreted (◐)
- Individual wipe pattern shapes (IDs 002–217): exist as distinct patterns shown graphically in OM pp. ~97–101, but textual descriptions are not available in extracted source
- Individual slide pattern shapes (IDs 222–248): exist as distinct patterns shown graphically in OM, split between "Normal Type" and "Sequential Type" but the split point is not extracted
- Intermediate effect variant names where OM shows only first and last variants (e.g. STROBE2 through STROBE9 inferred)
- Last-variant names where OM shows the family name without a numeric suffix (interpreted as the highest-numbered variant)

### Flagged as missing (⚠)
- Power consumption figure
- Input / output signal levels and impedances (formal spec table)
- Absolute time durations for Transformer duration digits 0–9
- Mathematical shape of Video Fader Curves A, B, C
- Numeric ranges for the Brightness proc-amp, Video Sync Threshold, Video Locate/Range, and OSD Locate parameters (present in menu but not cleanly extractable from PDF text)
- Distinction between W-LUMIKEY presets 53 and 54, B-LUMIKEY 55 and 56, and CHROMAKEY 57 and 58 (they exist as distinct slots but their default parameter values are not published)
- Distinction between STILL1 (ID 01) and STILL2 (ID 02) — likely field-freeze vs frame-freeze per B&H product copy, but not confirmed in OM
- Factory-default specific variants for each of the eight effect buttons in Memory slot 1
- Complete pairwise combinability matrix for simultaneously-usable effects
- BPM range (minimum / maximum displayable tempo)
- AutoB/W output-fade timing
- Named values of the 0–15 Border/Shadow color palettes in PinP setup
- Specific function mapping of the eight Note Mode MIDI note numbers (which note triggers which control)
- Complete SysEx address table (present in OM pp. 92–95, not in extracted text)
- Preview Display Mode differences between Modes 1, 2, and 3
- ChromaKey Color axis hue-mapping (linear vs weighted)
- Individual variant differences within each effect family (e.g. what STROBE1 vs STROBE5 actually differ in)

### Confidence summary
- **High confidence**: all §3 signal specs, §4 connector counts, §5–§6 panel layouts, §7 signal flow, §8 memory system structure, §9 transition counts by family, §10 effect IDs and family groupings, §11–§13 key and PinP parameter ranges, §14 Transformer behaviors, §15–§16 output-fade and BPM-sync behaviors, §19 MIDI chart, §21 Presentation Mode mapping.
- **Medium confidence** (interpreted from consistent OM conventions): intermediate effect-variant names between first and last listed entries; Key transition ID-to-description mapping.
- **Low confidence / explicitly missing**: everything marked ⚠ above.

---

*Document compiled from the Edirol V-4 Owner's Manual (Roland Corporation, © 2003, MIDI Implementation v1.00 dated Dec 13, 2002), Roland/Edirol product literature, the Videomaker V-4 review (2018 archive), and B&H Photo Video product page. Where source material is limited, gaps are explicitly flagged rather than omitted.*
