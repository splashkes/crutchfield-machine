// input.h — action registry, bindings, and input dispatch.
//
// The rest of the app talks to Input in terms of *actions* — named things
// like "warp zoom up" or "toggle recording". Input owns the map from raw
// sources (keyboard keys, gamepad buttons/axes, future MIDI CCs) to
// actions, so rebinding is a config-file edit and adding a new input
// source is a new poller, not a new switch-case in main.
//
// Commit 1 wires only keyboard. Commit 2 adds gamepad + MIDI stubs.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────
// Action catalogue. Each entry is either:
//   STEP     — fires once per keypress (or repeat). magnitude = signed
//              nudge in "steps" (caller scales to raw delta). Shift/Ctrl
//              multipliers are applied before dispatch.
//   RATE     — continuous; magnitude is integrated per frame. Used by
//              gamepad axes once C2 lands. Keyboard does not emit RATE.
//   DISCRETE — fires once; magnitude ignored (or 0.0 on release for
//              "held" semantics below).
//   TRIGGER  — fires once on press, once on release (magnitude 1.0 /
//              0.0 respectively). For SPACE-hold-inject.
// ─────────────────────────────────────────────────────────────────────────
enum ActionId : int {
    ACT_NONE = 0,

    // ── Layer toggles ────────────────────────────────────────────────
    ACT_LAYER_WARP, ACT_LAYER_OPTICS, ACT_LAYER_GAMMA, ACT_LAYER_COLOR,
    ACT_LAYER_CONTRAST, ACT_LAYER_DECAY, ACT_LAYER_NOISE, ACT_LAYER_COUPLE,
    ACT_LAYER_EXTERNAL, ACT_LAYER_INJECT, ACT_LAYER_PHYSICS, ACT_LAYER_THERMAL,
    // Cursor-based layer navigation for gamepad (D-pad L/R in Layers section)
    ACT_LAYER_CURSOR_UP, ACT_LAYER_CURSOR_DN,
    ACT_LAYER_TOGGLE_ARMED,

    // ── Warp (STEP; Rate variants map to same ActionId when C2 lands) ─
    ACT_ZOOM_UP, ACT_ZOOM_DN,
    ACT_THETA_UP, ACT_THETA_DN,
    ACT_TRANS_LEFT, ACT_TRANS_RIGHT, ACT_TRANS_UP, ACT_TRANS_DN,

    // ── Optics ───────────────────────────────────────────────────────
    ACT_CHROMA_UP, ACT_CHROMA_DN,
    ACT_BLURX_UP, ACT_BLURX_DN,
    ACT_BLURY_UP, ACT_BLURY_DN,
    ACT_BLURANG_UP, ACT_BLURANG_DN,

    // ── Color / tone ─────────────────────────────────────────────────
    ACT_GAMMA_UP, ACT_GAMMA_DN,
    ACT_HUE_UP, ACT_HUE_DN,
    ACT_SAT_UP, ACT_SAT_DN,
    ACT_CONTRAST_UP, ACT_CONTRAST_DN,

    // ── Dynamics ─────────────────────────────────────────────────────
    ACT_DECAY_UP, ACT_DECAY_DN,
    ACT_NOISE_UP, ACT_NOISE_DN,
    ACT_COUPLE_UP, ACT_COUPLE_DN,
    ACT_EXTERNAL_UP, ACT_EXTERNAL_DN,

    // ── Physics (Crutchfield camera-side knobs) ──────────────────────
    ACT_INVERT_TOGGLE,
    ACT_SENSORGAMMA_UP, ACT_SENSORGAMMA_DN,
    ACT_SATKNEE_UP, ACT_SATKNEE_DN,
    ACT_COLORCROSS_UP, ACT_COLORCROSS_DN,

    // ── Thermal ──────────────────────────────────────────────────────
    ACT_THERMAMP_UP, ACT_THERMAMP_DN,
    ACT_THERMSCALE_UP, ACT_THERMSCALE_DN,
    ACT_THERMSPEED_UP, ACT_THERMSPEED_DN,
    ACT_THERMRISE_UP, ACT_THERMRISE_DN,
    ACT_THERMSWIRL_UP, ACT_THERMSWIRL_DN,

    // ── Patterns + inject ────────────────────────────────────────────
    ACT_PATTERN_HBARS, ACT_PATTERN_VBARS, ACT_PATTERN_DOT,
    ACT_PATTERN_CHECKER, ACT_PATTERN_GRAD,
    ACT_PATTERN_NOISE, ACT_PATTERN_RINGS, ACT_PATTERN_SPIRAL,
    ACT_PATTERN_POLKA, ACT_PATTERN_STARBURST,
    ACT_PATTERN_ANIM_BOUNCER,
    ACT_SHAPE_TRIANGLE_HOLD, ACT_SHAPE_STAR_HOLD,
    ACT_SHAPE_CIRCLE_HOLD, ACT_SHAPE_SQUARE_HOLD,
    ACT_INJECT_HOLD,      // TRIGGER: 1.0 on press, 0.0 on release

    // ── App-level actions ────────────────────────────────────────────
    ACT_CLEAR,
    ACT_PAUSE,
    ACT_HELP,
    ACT_HELP_UP, ACT_HELP_DN, ACT_HELP_ENTER, ACT_HELP_BACK,
    ACT_RELOAD_SHADERS,
    ACT_FULLSCREEN,
    ACT_REC_TOGGLE,
    ACT_SCREENSHOT,
    ACT_PRESET_SAVE, ACT_PRESET_NEXT, ACT_PRESET_PREV,
    ACT_BLURQ_CYCLE, ACT_CAQ_CYCLE, ACT_NOISEQ_CYCLE, ACT_FIELDS_CYCLE,
    ACT_PIXELATE_STYLE_CYCLE,
    ACT_PIXELATE_BLEED_CYCLE,
    ACT_PIXELATE_BURN_RESEED,
    // Cursor-based quality navigation (D-pad L/R + A in Quality section)
    ACT_QUALITY_CURSOR_UP, ACT_QUALITY_CURSOR_DN, ACT_QUALITY_FIRE_ARMED,
    // Cursor-based pattern navigation (D-pad L/R in Inject section)
    ACT_PATTERN_CURSOR_UP, ACT_PATTERN_CURSOR_DN,
    ACT_PRINT_HELP_STDOUT,
    ACT_QUIT,

    // ── V-4 effect slots (C4+) ───────────────────────────────────────
    ACT_VFX1_CYCLE_FWD, ACT_VFX1_CYCLE_BACK, ACT_VFX1_OFF,
    ACT_VFX1_PARAM_UP, ACT_VFX1_PARAM_DN,
    ACT_VFX1_BSRC_CYCLE,
    ACT_VFX1_PAD_0, ACT_VFX1_PAD_1, ACT_VFX1_PAD_2, ACT_VFX1_PAD_3,
    ACT_VFX1_PAD_4, ACT_VFX1_PAD_5, ACT_VFX1_PAD_6, ACT_VFX1_PAD_7,
    ACT_VFX2_CYCLE_FWD, ACT_VFX2_CYCLE_BACK, ACT_VFX2_OFF,
    ACT_VFX2_PARAM_UP, ACT_VFX2_PARAM_DN,
    ACT_VFX2_BSRC_CYCLE,

    // ── Output fade (C6) ─────────────────────────────────────────────
    ACT_OUTFADE_UP, ACT_OUTFADE_DN,
    ACT_OUTFADE_AXIS,   // RATE/AXIS: absolute value in [-1, +1]
    // Display-only brightness multiplier (applied in blit, not feedback).
    ACT_BRIGHTNESS_UP, ACT_BRIGHTNESS_DN,

    // ── Bipolar axis variants (signed magnitude; gamepad sticks) ─────
    // For sticks, mag is -1..+1 each frame. apply_action scales by the
    // same step::X as keyboard so a full-deflection axis drifts the
    // parameter roughly at the same rate as holding the corresponding
    // key. "None" in the middle (deadzone applied in Input::pollGamepad).
    ACT_ZOOM_AXIS, ACT_THETA_AXIS,
    ACT_TRANS_X_AXIS, ACT_TRANS_Y_AXIS,
    ACT_HUE_AXIS,
    ACT_DECAY_AXIS,
    ACT_EXTERNAL_AXIS, // absolute 0..1 external/camera blend
    ACT_SHAPE_COUNT_AXIS, // absolute 0..1 shape count, mapped to 1..16

    // ── Music / MIDI integration ─────────────────────────────────────
    // Launches loopMIDI if installed. No-op if a MIDI port already exists.
    ACT_LAUNCH_LOOPMIDI,

    // Native music engine — preset cycle + play/pause.
    ACT_MUSIC_NEXT,
    ACT_MUSIC_PREV,
    ACT_MUSIC_PLAYPAUSE,

    // ── BPM (C7) ─────────────────────────────────────────────────────
    ACT_BPM_TAP,
    ACT_BPM_SYNC_TOGGLE,
    ACT_BPM_DIV_CYCLE,
    ACT_BPM_INJECT_TOGGLE,
    ACT_BPM_STROBE_TOGGLE,
    ACT_BPM_VFXCYCLE_TOGGLE,
    ACT_BPM_FLASH_TOGGLE,
    ACT_BPM_DECAYDIP_TOGGLE,
    ACT_BPM_HUEJUMP_TOGGLE,
    ACT_BPM_HUEJUMP_STEP_UP, ACT_BPM_HUEJUMP_STEP_DN,
    ACT_BPM_INVERT_TOGGLE,
    ACT_BPM_INVERT_DIV_UP, ACT_BPM_INVERT_DIV_DN,

    ACT__COUNT
};

enum ActionKind : int { AK_STEP, AK_RATE, AK_DISCRETE, AK_TRIGGER };

// Binding sources. GAMEPAD_* are populated in C2, MIDI_* by the Strudel
// integration pass. For MIDI: `modmask` is MIDI channel (1..16), or 0 for
// "omni" (match any channel).
enum BindSource : int {
    SRC_NONE = 0,
    SRC_KEY,           // code = GLFW_KEY_*, modmask = GLFW_MOD_* (Shift usually stripped)
    SRC_GAMEPAD_BTN,   // code = GLFW_GAMEPAD_BUTTON_*
    SRC_GAMEPAD_AXIS,  // code = GLFW_GAMEPAD_AXIS_* (bipolar/rate)
    SRC_MIDI_CC,       // code = CC number,        modmask = channel (0 = omni)
    SRC_MIDI_CC14,     // code = CC MSB number,    modmask = channel (0 = omni)
    SRC_MIDI_NOTE,     // code = MIDI note number, modmask = channel (0 = omni)
};

// Gamepad binding context — what "mode" the controller is in. Keyboard
// always uses CTX_ANY (keyboard is never contextually remapped).
//
// The numeric layout is load-bearing: CTX_SEC_STATUS + N == section index
// N. So adding a new help section only needs a new enum entry and a
// matching HELP_SECTIONS[] entry on the main.cpp side.
enum BindContext : int {
    CTX_ANY = 0,          // always active (e.g. Back = help toggle)
    CTX_MENU,             // only in help menu view
    CTX_SEC_STATUS,       // section indices follow the HELP_SECTIONS order
    CTX_SEC_LAYERS,
    CTX_SEC_WARP,
    CTX_SEC_OPTICS,
    CTX_SEC_COLOR,
    CTX_SEC_DYN,
    CTX_SEC_PHYSICS,
    CTX_SEC_THERMAL,
    CTX_SEC_INJECT,
    CTX_SEC_VFX1,
    CTX_SEC_VFX2,
    CTX_SEC_OUTPUT,
    CTX_SEC_BPM,
    CTX_SEC_MUSIC,
    CTX_SEC_QUALITY,
    CTX_SEC_APP,
    CTX_SEC_BINDINGS,
    CTX__COUNT
};

struct Binding {
    ActionId  action   = ACT_NONE;
    BindSource source  = SRC_NONE;
    int       code     = 0;
    int       modmask  = 0;        // for KEY: exact match on Ctrl/Alt/Super
    float     scale    = 1.0f;     // multiplier applied to step/axis value
    bool      invert   = false;    // negate value
    float     deadzone = 0.0f;     // axis deadzone (ignored for keys)
    bool      absolute = false;    // axis: dispatch position directly (no
                                   //   dt integration). Natural for
                                   //   "the stick IS the knob" mappings
                                   //   like output fade. Self-centers.
    bool      relative = false;    // MIDI CC: signed 0x40-centered delta
    bool      delta    = false;    // MIDI CC/CC14: dispatch change since last value
    bool      bipolar  = false;    // MIDI CC/CC14 absolute: 0..1 -> -1..+1
    bool      shifted  = false;    // MIDI note: require software Shift note held
    BindContext context = CTX_ANY; // gamepad only; keyboard ignores this
};


struct ActionInfo {
    ActionId   id;
    const char* name;              // stable identifier used in bindings.ini
    ActionKind kind;
    const char* group;             // for drill-down help UI
    const char* desc;              // one-line label
};

// Lookup by id or by name. Returns a pointer to a static table entry or null.
const ActionInfo* action_info(ActionId id);
const ActionInfo* action_info_by_name(const char* name);

// Total count of entries in the action_info table.
int action_info_count();

// ─────────────────────────────────────────────────────────────────────────
// Input — stateful: holds the current binding list and a dispatcher callback.
// ─────────────────────────────────────────────────────────────────────────
class Input {
public:
    using Handler = std::function<void(ActionId id, float magnitude)>;

    void setHandler(Handler h) { handler_ = std::move(h); }

    // Install the built-in default keyboard map (matches pre-refactor
    // behavior byte-for-byte).
    void installDefaults();

    // Clear all bindings.
    void clear() { bindings_.clear(); }

    // Read bindings.ini if present (merged over defaults — file entries
    // override defaults that target the same action). On macOS, missing
    // Command-key aliases are backfilled after load so legacy configs stay
    // usable on Apple keyboards. Returns true if file was present & parsed
    // without error; returns false (and leaves the current map intact) if
    // the file wasn't there.
    bool loadIni(const std::string& path);

    // Write current bindings.ini (commented, grouped by action group).
    bool saveIni(const std::string& path) const;

    // GLFW key callback. Translates raw key presses to ActionId dispatches.
    void onKey(int key, int scancode, int action, int mods);

    // Poll connected gamepad once per frame. jid is a GLFW joystick id
    // (typically GLFW_JOYSTICK_1). `currentCtx` is the gamepad context
    // the caller is in — pollGamepad filters bindings to those whose
    // context is either CTX_ANY or `currentCtx`. Pass dt as received
    // from glfwGetTime delta.
    void pollGamepad(int jid, float dt, BindContext currentCtx);

    // MIDI input — opens a platform backend on first call (winmm on Windows,
    // CoreMIDI on macOS), retries if a port becomes available later. Drains
    // the callback-populated queue on the main thread and dispatches Note/CC
    // through the existing handler_. MIDI Clock / Start / Stop update midi_
    // state which main.cpp reads.
    void pollMidi(float dt);

    // Live MIDI status — snapshot exposed to main.cpp for the BPM section
    // display and for tempo-following. All fields are writer-owned by
    // pollMidi; consumers may clear the `startPending` / `stopPending`
    // flags after handling the event.
    struct MidiState {
        std::string portName;           // "" if not connected
        bool  connected   = false;      // port open
        bool  clockLive   = false;      // F8 arrived within ~500ms
        float derivedBpm  = 0.0f;       // from rolling window of Clock pulses
        // Last events received (for display / debugging).
        int   lastNoteCh  = 0;          // 1..16, 0 = nothing yet
        int   lastNoteNum = -1;
        int   lastNoteVel = 0;
        int   lastCcCh    = 0;
        int   lastCcNum   = -1;
        int   lastCcVal   = 0;
        bool  deck1Shift  = false;
        bool  deck2Shift  = false;
        // Pending system real-time events — caller consumes by setting back
        // to false after handling.
        bool  startPending = false;
        bool  stopPending  = false;
    };
    MidiState&       midi()       { return midi_; }
    const MidiState& midi() const { return midi_; }

    // Preferred port name, parsed from `[midi] port = …` in bindings.ini.
    // Substring match against enumerated input devices; empty = pick the
    // first loopMIDI-like port found. Set before the first pollMidi call.
    void setMidiPortHint(const std::string& s) { midiPortHint_ = s; }
    void setMidiLearn(bool enabled) { midiLearn_ = enabled; }
    bool sendMidiNote(int channel, int note, int velocity);

    // Low-level insert (used by installDefaults and loadIni).
    void bind(const Binding& b) { bindings_.push_back(b); }

    // Read-only access (help UI wants to print "Q/A  zoom" style rows).
    const std::vector<Binding>& bindings() const { return bindings_; }

private:
    std::vector<Binding> bindings_;
    Handler handler_;
    MidiState   midi_;
    std::string midiPortHint_;
    bool        midiLearn_ = false;
};

// Global instance; defined in input.cpp.
extern Input g_input;
