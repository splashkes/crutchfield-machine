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
    ACT_INJECT_HOLD,      // TRIGGER: 1.0 on press, 0.0 on release

    // ── App-level actions ────────────────────────────────────────────
    ACT_CLEAR,
    ACT_PAUSE,
    ACT_HELP,
    ACT_HELP_UP, ACT_HELP_DN, ACT_HELP_ENTER, ACT_HELP_BACK,
    ACT_RELOAD_SHADERS,
    ACT_FULLSCREEN,
    ACT_REC_TOGGLE,
    ACT_PRESET_SAVE, ACT_PRESET_NEXT, ACT_PRESET_PREV,
    ACT_BLURQ_CYCLE, ACT_CAQ_CYCLE, ACT_NOISEQ_CYCLE, ACT_FIELDS_CYCLE,
    ACT_PRINT_HELP_STDOUT,
    ACT_QUIT,

    // ── V-4 effect slots (C4+) ───────────────────────────────────────
    ACT_VFX1_CYCLE_FWD, ACT_VFX1_CYCLE_BACK, ACT_VFX1_OFF,
    ACT_VFX1_PARAM_UP, ACT_VFX1_PARAM_DN,
    ACT_VFX1_BSRC_CYCLE,
    ACT_VFX2_CYCLE_FWD, ACT_VFX2_CYCLE_BACK, ACT_VFX2_OFF,
    ACT_VFX2_PARAM_UP, ACT_VFX2_PARAM_DN,
    ACT_VFX2_BSRC_CYCLE,

    // ── Output fade (C6) ─────────────────────────────────────────────
    ACT_OUTFADE_UP, ACT_OUTFADE_DN,
    ACT_OUTFADE_AXIS,   // RATE/AXIS: absolute value in [-1, +1]

    // ── Bipolar axis variants (signed magnitude; gamepad sticks) ─────
    // For sticks, mag is -1..+1 each frame. apply_action scales by the
    // same step::X as keyboard so a full-deflection axis drifts the
    // parameter roughly at the same rate as holding the corresponding
    // key. "None" in the middle (deadzone applied in Input::pollGamepad).
    ACT_ZOOM_AXIS, ACT_THETA_AXIS,
    ACT_TRANS_X_AXIS, ACT_TRANS_Y_AXIS,
    ACT_HUE_AXIS,
    ACT_DECAY_AXIS,

    // ── BPM (C7) ─────────────────────────────────────────────────────
    ACT_BPM_TAP,
    ACT_BPM_SYNC_TOGGLE,
    ACT_BPM_DIV_CYCLE,
    ACT_BPM_INJECT_TOGGLE,
    ACT_BPM_STROBE_TOGGLE,
    ACT_BPM_VFXCYCLE_TOGGLE,
    ACT_BPM_FLASH_TOGGLE,
    ACT_BPM_DECAYDIP_TOGGLE,

    ACT__COUNT
};

enum ActionKind : int { AK_STEP, AK_RATE, AK_DISCRETE, AK_TRIGGER };

// Binding sources. GAMEPAD_* are populated in C2, MIDI_CC in a later pass.
enum BindSource : int {
    SRC_NONE = 0,
    SRC_KEY,           // code = GLFW_KEY_*, modmask = GLFW_MOD_*
    SRC_GAMEPAD_BTN,   // code = GLFW_GAMEPAD_BUTTON_*
    SRC_GAMEPAD_AXIS,  // code = GLFW_GAMEPAD_AXIS_* (bipolar/rate)
    SRC_MIDI_CC,       // code = CC number
};

struct Binding {
    ActionId  action   = ACT_NONE;
    BindSource source  = SRC_NONE;
    int       code     = 0;
    int       modmask  = 0;        // for KEY: exact match on Ctrl/Alt/Shift
    float     scale    = 1.0f;     // multiplier applied to step/axis value
    bool      invert   = false;    // negate value
    float     deadzone = 0.0f;     // axis deadzone (ignored for keys)
    bool      absolute = false;    // axis: dispatch position directly (no
                                   //   dt integration). Natural for
                                   //   "the stick IS the knob" mappings
                                   //   like output fade. Self-centers.
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
    // override defaults that target the same action). Returns true if file
    // was present & parsed without error; returns false (and leaves the
    // current map intact) if the file wasn't there.
    bool loadIni(const std::string& path);

    // Write current bindings.ini (commented, grouped by action group).
    bool saveIni(const std::string& path) const;

    // GLFW key callback. Translates raw key presses to ActionId dispatches.
    void onKey(int key, int scancode, int action, int mods);

    // Poll connected gamepad once per frame. jid is a GLFW joystick id
    // (typically GLFW_JOYSTICK_1). axisScale is an extra global multiplier
    // applied to RATE dispatches — the frame dt in effect, so at 60fps a
    // scale of 1.0 + dt=1/60 yields roughly keyboard-repeat-equivalent
    // rates. Pass dt as received from glfwGetTime delta.
    void pollGamepad(int jid, float dt);

    // MIDI stub — populated by a future commit (RtMidi / winmm). Kept here
    // so apply_action consumers never have to care where events originate.
    void pollMidi(float dt);

    // Low-level insert (used by installDefaults and loadIni).
    void bind(const Binding& b) { bindings_.push_back(b); }

    // Read-only access (help UI wants to print "Q/A  zoom" style rows).
    const std::vector<Binding>& bindings() const { return bindings_; }

private:
    std::vector<Binding> bindings_;
    Handler handler_;
};

// Global instance; defined in input.cpp.
extern Input g_input;
