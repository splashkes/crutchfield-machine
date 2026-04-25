// input.cpp — action registry + binding resolver.

#include "input.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <mutex>

Input g_input;

struct FeedbackMidiMsg {
    uint8_t b[3];
    uint8_t len;
};

#ifdef __APPLE__
extern "C" {
int  feedback_midi_open(const char* port_hint);
int  feedback_midi_poll(FeedbackMidiMsg* out, int max_count);
void feedback_midi_status(char* name, int name_cap, int* connected);
int  feedback_midi_send_note(int channel, int note, int velocity);
}
#endif

// ─────────────────────────────────────────────────────────────────────────
// Action catalogue — the table that names every ActionId. Order is free
// but stable names matter because bindings.ini refers to them by name.
// ─────────────────────────────────────────────────────────────────────────
static const ActionInfo ACTIONS[] = {
    // layers
    { ACT_LAYER_WARP,      "layer.warp",      AK_DISCRETE, "Layers", "toggle warp" },
    { ACT_LAYER_OPTICS,    "layer.optics",    AK_DISCRETE, "Layers", "toggle optics" },
    { ACT_LAYER_GAMMA,     "layer.gamma",     AK_DISCRETE, "Layers", "toggle gamma" },
    { ACT_LAYER_COLOR,     "layer.color",     AK_DISCRETE, "Layers", "toggle color" },
    { ACT_LAYER_CONTRAST,  "layer.contrast",  AK_DISCRETE, "Layers", "toggle contrast" },
    { ACT_LAYER_DECAY,     "layer.decay",     AK_DISCRETE, "Layers", "toggle decay" },
    { ACT_LAYER_NOISE,     "layer.noise",     AK_DISCRETE, "Layers", "toggle noise" },
    { ACT_LAYER_COUPLE,    "layer.couple",    AK_DISCRETE, "Layers", "toggle couple" },
    { ACT_LAYER_EXTERNAL,  "layer.external",  AK_DISCRETE, "Layers", "toggle external (camera)" },
    { ACT_LAYER_INJECT,    "layer.inject",    AK_DISCRETE, "Layers", "toggle inject" },
    { ACT_LAYER_PHYSICS,   "layer.physics",   AK_DISCRETE, "Layers", "toggle physics" },
    { ACT_LAYER_THERMAL,   "layer.thermal",   AK_DISCRETE, "Layers", "toggle thermal" },
    { ACT_LAYER_CURSOR_UP, "layer.cursor.up", AK_DISCRETE, "Layers", "cursor prev" },
    { ACT_LAYER_CURSOR_DN, "layer.cursor.dn", AK_DISCRETE, "Layers", "cursor next" },
    { ACT_LAYER_TOGGLE_ARMED, "layer.toggleArmed", AK_DISCRETE, "Layers", "toggle armed layer" },

    // warp
    { ACT_ZOOM_UP,   "warp.zoom+",      AK_STEP, "Warp", "zoom +" },
    { ACT_ZOOM_DN,   "warp.zoom-",      AK_STEP, "Warp", "zoom -" },
    { ACT_THETA_UP,  "warp.theta+",     AK_STEP, "Warp", "rotation +" },
    { ACT_THETA_DN,  "warp.theta-",     AK_STEP, "Warp", "rotation -" },
    { ACT_TRANS_LEFT,  "warp.trans.left",  AK_STEP, "Warp", "translate -X" },
    { ACT_TRANS_RIGHT, "warp.trans.right", AK_STEP, "Warp", "translate +X" },
    { ACT_TRANS_UP,    "warp.trans.up",    AK_STEP, "Warp", "translate -Y" },
    { ACT_TRANS_DN,    "warp.trans.down",  AK_STEP, "Warp", "translate +Y" },

    // optics
    { ACT_CHROMA_UP,  "optics.chroma+",   AK_STEP, "Optics", "chroma aberration +" },
    { ACT_CHROMA_DN,  "optics.chroma-",   AK_STEP, "Optics", "chroma aberration -" },
    { ACT_BLURX_UP,   "optics.blurX+",    AK_STEP, "Optics", "blur X +" },
    { ACT_BLURX_DN,   "optics.blurX-",    AK_STEP, "Optics", "blur X -" },
    { ACT_BLURY_UP,   "optics.blurY+",    AK_STEP, "Optics", "blur Y +" },
    { ACT_BLURY_DN,   "optics.blurY-",    AK_STEP, "Optics", "blur Y -" },
    { ACT_BLURANG_UP, "optics.blurAng+",  AK_STEP, "Optics", "blur angle +" },
    { ACT_BLURANG_DN, "optics.blurAng-",  AK_STEP, "Optics", "blur angle -" },

    // color/tone
    { ACT_GAMMA_UP,    "color.gamma+",    AK_STEP, "Color", "gamma +" },
    { ACT_GAMMA_DN,    "color.gamma-",    AK_STEP, "Color", "gamma -" },
    { ACT_HUE_UP,      "color.hueRate+",  AK_STEP, "Color", "hue rate +" },
    { ACT_HUE_DN,      "color.hueRate-",  AK_STEP, "Color", "hue rate -" },
    { ACT_SAT_UP,      "color.sat+",      AK_STEP, "Color", "saturation +" },
    { ACT_SAT_DN,      "color.sat-",      AK_STEP, "Color", "saturation -" },
    { ACT_CONTRAST_UP, "color.contrast+", AK_STEP, "Color", "contrast +" },
    { ACT_CONTRAST_DN, "color.contrast-", AK_STEP, "Color", "contrast -" },

    // dynamics
    { ACT_DECAY_UP,    "dyn.decay+",    AK_STEP, "Dynamics", "decay +" },
    { ACT_DECAY_DN,    "dyn.decay-",    AK_STEP, "Dynamics", "decay -" },
    { ACT_NOISE_UP,    "dyn.noise+",    AK_STEP, "Dynamics", "noise +" },
    { ACT_NOISE_DN,    "dyn.noise-",    AK_STEP, "Dynamics", "noise -" },
    { ACT_COUPLE_UP,   "dyn.couple+",   AK_STEP, "Dynamics", "couple +" },
    { ACT_COUPLE_DN,   "dyn.couple-",   AK_STEP, "Dynamics", "couple -" },
    { ACT_EXTERNAL_UP, "dyn.external+", AK_STEP, "Dynamics", "external (cam) +" },
    { ACT_EXTERNAL_DN, "dyn.external-", AK_STEP, "Dynamics", "external (cam) -" },

    // physics
    { ACT_INVERT_TOGGLE,    "phys.invert",        AK_DISCRETE, "Physics", "invert toggle" },
    { ACT_SENSORGAMMA_UP,   "phys.sensorGamma+",  AK_STEP, "Physics", "sensor gamma +" },
    { ACT_SENSORGAMMA_DN,   "phys.sensorGamma-",  AK_STEP, "Physics", "sensor gamma -" },
    { ACT_SATKNEE_UP,       "phys.satKnee+",      AK_STEP, "Physics", "sat knee +" },
    { ACT_SATKNEE_DN,       "phys.satKnee-",      AK_STEP, "Physics", "sat knee -" },
    { ACT_COLORCROSS_UP,    "phys.colorCross+",   AK_STEP, "Physics", "color cross +" },
    { ACT_COLORCROSS_DN,    "phys.colorCross-",   AK_STEP, "Physics", "color cross -" },

    // thermal
    { ACT_THERMAMP_UP,    "therm.amp+",    AK_STEP, "Thermal", "amplitude +" },
    { ACT_THERMAMP_DN,    "therm.amp-",    AK_STEP, "Thermal", "amplitude -" },
    { ACT_THERMSCALE_UP,  "therm.scale+",  AK_STEP, "Thermal", "scale +" },
    { ACT_THERMSCALE_DN,  "therm.scale-",  AK_STEP, "Thermal", "scale -" },
    { ACT_THERMSPEED_UP,  "therm.speed+",  AK_STEP, "Thermal", "speed +" },
    { ACT_THERMSPEED_DN,  "therm.speed-",  AK_STEP, "Thermal", "speed -" },
    { ACT_THERMRISE_UP,   "therm.rise+",   AK_STEP, "Thermal", "rise +" },
    { ACT_THERMRISE_DN,   "therm.rise-",   AK_STEP, "Thermal", "rise -" },
    { ACT_THERMSWIRL_UP,  "therm.swirl+",  AK_STEP, "Thermal", "swirl +" },
    { ACT_THERMSWIRL_DN,  "therm.swirl-",  AK_STEP, "Thermal", "swirl -" },

    // inject/pattern
    { ACT_PATTERN_HBARS,   "pattern.hbars",   AK_DISCRETE, "Inject", "pattern: H-bars" },
    { ACT_PATTERN_VBARS,   "pattern.vbars",   AK_DISCRETE, "Inject", "pattern: V-bars" },
    { ACT_PATTERN_DOT,     "pattern.dot",     AK_DISCRETE, "Inject", "pattern: dot" },
    { ACT_PATTERN_CHECKER, "pattern.checker", AK_DISCRETE, "Inject", "pattern: checker" },
    { ACT_PATTERN_GRAD,    "pattern.grad",    AK_DISCRETE, "Inject", "pattern: gradient" },
    { ACT_SHAPE_TRIANGLE_HOLD, "shape.triangle.hold", AK_TRIGGER, "Inject", "shape: triangle hold" },
    { ACT_SHAPE_STAR_HOLD,     "shape.star.hold",     AK_TRIGGER, "Inject", "shape: star hold" },
    { ACT_SHAPE_CIRCLE_HOLD,   "shape.circle.hold",   AK_TRIGGER, "Inject", "shape: circle hold" },
    { ACT_SHAPE_SQUARE_HOLD,   "shape.square.hold",   AK_TRIGGER, "Inject", "shape: square hold" },
    { ACT_INJECT_HOLD,     "inject.hold",     AK_TRIGGER,  "Inject", "inject (hold)" },

    // app
    { ACT_CLEAR,            "app.clear",          AK_DISCRETE, "App", "clear fields" },
    { ACT_PAUSE,            "app.pause",          AK_DISCRETE, "App", "pause/resume" },
    { ACT_HELP,             "app.help",           AK_DISCRETE, "App", "help toggle" },
    { ACT_HELP_UP,          "help.up",            AK_DISCRETE, "App", "help cursor up" },
    { ACT_HELP_DN,          "help.down",          AK_DISCRETE, "App", "help cursor down" },
    { ACT_HELP_ENTER,       "help.enter",         AK_DISCRETE, "App", "help drill in" },
    { ACT_HELP_BACK,        "help.back",          AK_DISCRETE, "App", "help back / close" },
    { ACT_RELOAD_SHADERS,   "app.reloadShaders",  AK_DISCRETE, "App", "reload shaders" },
    { ACT_FULLSCREEN,       "app.fullscreen",     AK_DISCRETE, "App", "fullscreen toggle" },
    { ACT_REC_TOGGLE,       "rec.toggle",         AK_DISCRETE, "App", "recording start/stop" },
    { ACT_SCREENSHOT,       "app.screenshot",     AK_DISCRETE, "App", "screenshot (PNG, sim resolution, no HUD)" },
    { ACT_PRESET_SAVE,      "preset.save",        AK_DISCRETE, "App", "preset save" },
    { ACT_PRESET_NEXT,      "preset.next",        AK_DISCRETE, "App", "preset next" },
    { ACT_PRESET_PREV,      "preset.prev",        AK_DISCRETE, "App", "preset prev" },
    { ACT_BLURQ_CYCLE,      "q.blur",             AK_DISCRETE, "Quality", "cycle blur kernel" },
    { ACT_CAQ_CYCLE,        "q.ca",               AK_DISCRETE, "Quality", "cycle CA sampler" },
    { ACT_NOISEQ_CYCLE,     "q.noise",            AK_DISCRETE, "Quality", "cycle noise type" },
    { ACT_FIELDS_CYCLE,     "q.fields",           AK_DISCRETE, "Quality", "cycle coupled fields" },
    { ACT_QUALITY_CURSOR_UP,"q.cursor.up",        AK_DISCRETE, "Quality", "cursor prev" },
    { ACT_QUALITY_CURSOR_DN,"q.cursor.dn",        AK_DISCRETE, "Quality", "cursor next" },
    { ACT_QUALITY_FIRE_ARMED,"q.cycleArmed",      AK_DISCRETE, "Quality", "cycle armed quality" },
    { ACT_PATTERN_CURSOR_UP,"pattern.cursor.up",  AK_DISCRETE, "Inject", "pattern prev" },
    { ACT_PATTERN_CURSOR_DN,"pattern.cursor.dn",  AK_DISCRETE, "Inject", "pattern next" },
    { ACT_PRINT_HELP_STDOUT,"app.helpStdout",     AK_DISCRETE, "App", "print help to stdout" },
    { ACT_QUIT,             "app.quit",           AK_DISCRETE, "App", "quit" },

    // V-4 slots (bindings wired in C4+)
    { ACT_VFX1_CYCLE_FWD, "vfx1.next",      AK_DISCRETE, "VFX-1", "slot 1: next effect" },
    { ACT_VFX1_CYCLE_BACK,"vfx1.prev",      AK_DISCRETE, "VFX-1", "slot 1: prev effect" },
    { ACT_VFX1_OFF,       "vfx1.off",       AK_DISCRETE, "VFX-1", "slot 1: off" },
    { ACT_VFX1_PARAM_UP,  "vfx1.param+",    AK_STEP,     "VFX-1", "slot 1: param +" },
    { ACT_VFX1_PARAM_DN,  "vfx1.param-",    AK_STEP,     "VFX-1", "slot 1: param -" },
    { ACT_VFX1_BSRC_CYCLE,"vfx1.bsrc",      AK_DISCRETE, "VFX-1", "slot 1: cycle B-source" },
    { ACT_VFX1_PAD_0,     "vfx1.pad0",      AK_DISCRETE, "VFX-1", "slot 1: pad bank 1" },
    { ACT_VFX1_PAD_1,     "vfx1.pad1",      AK_DISCRETE, "VFX-1", "slot 1: pad bank 2" },
    { ACT_VFX1_PAD_2,     "vfx1.pad2",      AK_DISCRETE, "VFX-1", "slot 1: pad bank 3" },
    { ACT_VFX1_PAD_3,     "vfx1.pad3",      AK_DISCRETE, "VFX-1", "slot 1: pad bank 4" },
    { ACT_VFX1_PAD_4,     "vfx1.pad4",      AK_DISCRETE, "VFX-1", "slot 1: pad bank 5" },
    { ACT_VFX1_PAD_5,     "vfx1.pad5",      AK_DISCRETE, "VFX-1", "slot 1: pad bank 6" },
    { ACT_VFX1_PAD_6,     "vfx1.pad6",      AK_DISCRETE, "VFX-1", "slot 1: pad bank 7" },
    { ACT_VFX1_PAD_7,     "vfx1.pad7",      AK_DISCRETE, "VFX-1", "slot 1: pad bank 8" },
    { ACT_VFX2_CYCLE_FWD, "vfx2.next",      AK_DISCRETE, "VFX-2", "slot 2: next effect" },
    { ACT_VFX2_CYCLE_BACK,"vfx2.prev",      AK_DISCRETE, "VFX-2", "slot 2: prev effect" },
    { ACT_VFX2_OFF,       "vfx2.off",       AK_DISCRETE, "VFX-2", "slot 2: off" },
    { ACT_VFX2_PARAM_UP,  "vfx2.param+",    AK_STEP,     "VFX-2", "slot 2: param +" },
    { ACT_VFX2_PARAM_DN,  "vfx2.param-",    AK_STEP,     "VFX-2", "slot 2: param -" },
    { ACT_VFX2_BSRC_CYCLE,"vfx2.bsrc",      AK_DISCRETE, "VFX-2", "slot 2: cycle B-source" },

    // output fade
    { ACT_OUTFADE_UP,   "outfade.up",   AK_STEP,     "Output", "fade toward white" },
    { ACT_OUTFADE_DN,   "outfade.down", AK_STEP,     "Output", "fade toward black" },
    { ACT_OUTFADE_AXIS, "outfade.axis", AK_RATE,     "Output", "fade (axis -1..+1)" },

    // bipolar axis variants (gamepad sticks)
    { ACT_ZOOM_AXIS,    "warp.zoom.axis",   AK_RATE, "Warp",     "zoom (axis)" },
    { ACT_THETA_AXIS,   "warp.theta.axis",  AK_RATE, "Warp",     "rotation (axis)" },
    { ACT_TRANS_X_AXIS, "warp.transX.axis", AK_RATE, "Warp",     "translate X (axis)" },
    { ACT_TRANS_Y_AXIS, "warp.transY.axis", AK_RATE, "Warp",     "translate Y (axis)" },
    { ACT_HUE_AXIS,     "color.hue.axis",   AK_RATE, "Color",    "hue rate (axis)" },
    { ACT_DECAY_AXIS,   "dyn.decay.axis",   AK_RATE, "Dynamics", "decay (axis)" },
    { ACT_EXTERNAL_AXIS,"dyn.external.axis",AK_RATE, "Dynamics", "external (axis)" },
    { ACT_SHAPE_COUNT_AXIS,"shape.count.axis",AK_RATE, "Inject", "shape count (axis)" },

    // BPM
    { ACT_BPM_TAP,               "bpm.tap",           AK_DISCRETE, "BPM", "tap tempo" },
    { ACT_BPM_SYNC_TOGGLE,       "bpm.sync",          AK_DISCRETE, "BPM", "BPM sync on/off" },
    { ACT_BPM_DIV_CYCLE,         "bpm.div",           AK_DISCRETE, "BPM", "cycle beat division (x2/1/½/¼)" },
    { ACT_BPM_INJECT_TOGGLE,     "bpm.injectOnBeat",  AK_DISCRETE, "BPM", "toggle inject-on-beat" },
    { ACT_BPM_STROBE_TOGGLE,     "bpm.strobeLock",    AK_DISCRETE, "BPM", "toggle strobe rate lock" },
    { ACT_BPM_VFXCYCLE_TOGGLE,   "bpm.vfxCycle",      AK_DISCRETE, "BPM", "toggle vfx auto-cycle on beat" },
    { ACT_BPM_FLASH_TOGGLE,      "bpm.flash",         AK_DISCRETE, "BPM", "toggle fade-flash on beat" },
    { ACT_BPM_DECAYDIP_TOGGLE,   "bpm.decayDip",      AK_DISCRETE, "BPM", "toggle decay-dip on beat" },
};
static constexpr int N_ACTIONS = (int)(sizeof(ACTIONS) / sizeof(ACTIONS[0]));

int action_info_count() { return N_ACTIONS; }

const ActionInfo* action_info(ActionId id) {
    for (int i = 0; i < N_ACTIONS; i++)
        if (ACTIONS[i].id == id) return &ACTIONS[i];
    return nullptr;
}

const ActionInfo* action_info_by_name(const char* name) {
    for (int i = 0; i < N_ACTIONS; i++)
        if (std::strcmp(ACTIONS[i].name, name) == 0) return &ACTIONS[i];
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────
// Defaults — match pre-refactor behavior exactly.
// The mod mask means "Ctrl-S is a different binding from S". So for bare
// keys we set mods=0; for Ctrl combos we set mods=GLFW_MOD_CONTROL.
// Shift is NOT in the modmask — Shift is the coarse-step multiplier and
// is handled in onKey before dispatch.
// ─────────────────────────────────────────────────────────────────────────
static void K(Input& in, ActionId a, int key, int mods = 0, float scale = 1.0f) {
    Binding b;
    b.action  = a;
    b.source  = SRC_KEY;
    b.code    = key;
    b.modmask = mods;
    b.scale   = scale;
    in.bind(b);
}

static void MIDI(Input& in, ActionId a, BindSource src, int code, int ch = 0,
                 float scale = 1.0f, bool invert = false, bool relative = false,
                 bool delta = false, bool absolute = false, bool bipolar = false,
                 bool shifted = false) {
    Binding b;
    b.action   = a;
    b.source   = src;
    b.code     = code;
    b.modmask  = ch;
    b.scale    = scale;
    b.invert   = invert;
    b.relative = relative;
    b.delta    = delta;
    b.absolute = absolute;
    b.bipolar  = bipolar;
    b.shifted  = shifted;
    in.bind(b);
}

#ifdef __APPLE__
static bool has_key_binding(const Input& in, ActionId action, int key, int mods,
                            BindContext ctx = CTX_ANY) {
    for (const Binding& b : in.bindings()) {
        if (b.action != action) continue;
        if (b.source != SRC_KEY) continue;
        if (b.context != ctx) continue;
        if (b.code != key) continue;
        if (b.modmask != mods) continue;
        return true;
    }
    return false;
}

static bool has_super_binding(const Input& in, ActionId action,
                              BindContext ctx = CTX_ANY) {
    for (const Binding& b : in.bindings()) {
        if (b.action != action) continue;
        if (b.source != SRC_KEY) continue;
        if (b.context != ctx) continue;
        if (b.modmask & GLFW_MOD_SUPER) return true;
    }
    return false;
}

static int add_mac_alias(Input& in, ActionId action, int key, int mods,
                         bool onlyIfMissing) {
    if (has_key_binding(in, action, key, mods)) return 0;
    if (onlyIfMissing && has_super_binding(in, action)) return 0;
    K(in, action, key, mods);
    return 1;
}

static int install_mac_keyboard_aliases(Input& in, bool onlyIfMissing) {
    int added = 0;

    // These are additive aliases for keys that are awkward or absent on
    // Apple keyboards. They do not replace the existing cross-platform map.
    added += add_mac_alias(in, ACT_FULLSCREEN,    GLFW_KEY_ENTER,     GLFW_MOD_SUPER, onlyIfMissing);
    added += add_mac_alias(in, ACT_SCREENSHOT,    GLFW_KEY_BACKSLASH, GLFW_MOD_SUPER, onlyIfMissing);
    added += add_mac_alias(in, ACT_PRESET_SAVE,   GLFW_KEY_S,         GLFW_MOD_SUPER, onlyIfMissing);
    added += add_mac_alias(in, ACT_PRESET_NEXT,   GLFW_KEY_N,         GLFW_MOD_SUPER, onlyIfMissing);
    added += add_mac_alias(in, ACT_PRESET_PREV,   GLFW_KEY_P,         GLFW_MOD_SUPER, onlyIfMissing);

    added += add_mac_alias(in, ACT_LAYER_PHYSICS, GLFW_KEY_P,         GLFW_MOD_SUPER | GLFW_MOD_ALT, onlyIfMissing);
    added += add_mac_alias(in, ACT_LAYER_THERMAL, GLFW_KEY_T,         GLFW_MOD_SUPER | GLFW_MOD_ALT, onlyIfMissing);
    added += add_mac_alias(in, ACT_BLURQ_CYCLE,   GLFW_KEY_B,         GLFW_MOD_SUPER | GLFW_MOD_ALT, onlyIfMissing);
    added += add_mac_alias(in, ACT_CAQ_CYCLE,     GLFW_KEY_C,         GLFW_MOD_SUPER | GLFW_MOD_ALT, onlyIfMissing);
    added += add_mac_alias(in, ACT_NOISEQ_CYCLE,  GLFW_KEY_N,         GLFW_MOD_SUPER | GLFW_MOD_ALT, onlyIfMissing);
    added += add_mac_alias(in, ACT_FIELDS_CYCLE,  GLFW_KEY_F,         GLFW_MOD_SUPER | GLFW_MOD_ALT, onlyIfMissing);

    added += add_mac_alias(in, ACT_THERMAMP_DN,   GLFW_KEY_1,         GLFW_MOD_SUPER | GLFW_MOD_ALT, onlyIfMissing);
    added += add_mac_alias(in, ACT_THERMAMP_UP,   GLFW_KEY_2,         GLFW_MOD_SUPER | GLFW_MOD_ALT, onlyIfMissing);
    added += add_mac_alias(in, ACT_THERMSCALE_DN, GLFW_KEY_3,         GLFW_MOD_SUPER | GLFW_MOD_ALT, onlyIfMissing);
    added += add_mac_alias(in, ACT_THERMSCALE_UP, GLFW_KEY_4,         GLFW_MOD_SUPER | GLFW_MOD_ALT, onlyIfMissing);
    added += add_mac_alias(in, ACT_THERMSPEED_DN, GLFW_KEY_5,         GLFW_MOD_SUPER | GLFW_MOD_ALT, onlyIfMissing);
    added += add_mac_alias(in, ACT_THERMSPEED_UP, GLFW_KEY_6,         GLFW_MOD_SUPER | GLFW_MOD_ALT, onlyIfMissing);
    added += add_mac_alias(in, ACT_THERMRISE_DN,  GLFW_KEY_7,         GLFW_MOD_SUPER | GLFW_MOD_ALT, onlyIfMissing);
    added += add_mac_alias(in, ACT_THERMRISE_UP,  GLFW_KEY_8,         GLFW_MOD_SUPER | GLFW_MOD_ALT, onlyIfMissing);
    added += add_mac_alias(in, ACT_THERMSWIRL_DN, GLFW_KEY_9,         GLFW_MOD_SUPER | GLFW_MOD_ALT, onlyIfMissing);
    added += add_mac_alias(in, ACT_THERMSWIRL_UP, GLFW_KEY_0,         GLFW_MOD_SUPER | GLFW_MOD_ALT, onlyIfMissing);

    return added;
}
#endif

void Input::installDefaults() {
    clear();
    Input& in = *this;

    // Layers
    K(in, ACT_LAYER_WARP,     GLFW_KEY_F1);
    K(in, ACT_LAYER_OPTICS,   GLFW_KEY_F2);
    K(in, ACT_LAYER_GAMMA,    GLFW_KEY_F3);
    K(in, ACT_LAYER_COLOR,    GLFW_KEY_F4);
    K(in, ACT_LAYER_CONTRAST, GLFW_KEY_F5);
    K(in, ACT_LAYER_DECAY,    GLFW_KEY_F6);
    K(in, ACT_LAYER_NOISE,    GLFW_KEY_F7);
    K(in, ACT_LAYER_COUPLE,   GLFW_KEY_F8);
    K(in, ACT_LAYER_EXTERNAL, GLFW_KEY_F9);
    K(in, ACT_LAYER_INJECT,   GLFW_KEY_F10);
    K(in, ACT_LAYER_PHYSICS,  GLFW_KEY_INSERT);
    K(in, ACT_LAYER_THERMAL,  GLFW_KEY_PAGE_DOWN);

    // Warp
    K(in, ACT_ZOOM_UP,  GLFW_KEY_Q);
    K(in, ACT_ZOOM_DN,  GLFW_KEY_A);
    K(in, ACT_THETA_UP, GLFW_KEY_W);
    K(in, ACT_THETA_DN, GLFW_KEY_S);          // plain S (Ctrl+S is preset.save below)
    K(in, ACT_TRANS_LEFT,  GLFW_KEY_LEFT);
    K(in, ACT_TRANS_RIGHT, GLFW_KEY_RIGHT);
    K(in, ACT_TRANS_UP,    GLFW_KEY_UP);
    K(in, ACT_TRANS_DN,    GLFW_KEY_DOWN);

    // Optics
    K(in, ACT_CHROMA_DN,  GLFW_KEY_LEFT_BRACKET);
    K(in, ACT_CHROMA_UP,  GLFW_KEY_RIGHT_BRACKET);
    K(in, ACT_BLURX_DN,   GLFW_KEY_SEMICOLON);
    K(in, ACT_BLURX_UP,   GLFW_KEY_APOSTROPHE);
    K(in, ACT_BLURY_DN,   GLFW_KEY_COMMA);
    K(in, ACT_BLURY_UP,   GLFW_KEY_PERIOD);
    K(in, ACT_BLURANG_DN, GLFW_KEY_MINUS);
    K(in, ACT_BLURANG_UP, GLFW_KEY_EQUAL);

    // Color
    K(in, ACT_GAMMA_UP,    GLFW_KEY_G);
    K(in, ACT_GAMMA_DN,    GLFW_KEY_B);
    K(in, ACT_HUE_UP,      GLFW_KEY_E);
    K(in, ACT_HUE_DN,      GLFW_KEY_D);
    K(in, ACT_SAT_UP,      GLFW_KEY_R);
    K(in, ACT_SAT_DN,      GLFW_KEY_F);
    K(in, ACT_CONTRAST_UP, GLFW_KEY_T);
    K(in, ACT_CONTRAST_DN, GLFW_KEY_Y);

    // Dynamics
    K(in, ACT_DECAY_UP,    GLFW_KEY_U);
    K(in, ACT_DECAY_DN,    GLFW_KEY_J);
    K(in, ACT_NOISE_UP,    GLFW_KEY_N);       // plain N (Ctrl+N = preset.next below)
    K(in, ACT_NOISE_DN,    GLFW_KEY_M);
    K(in, ACT_COUPLE_UP,   GLFW_KEY_K);
    K(in, ACT_COUPLE_DN,   GLFW_KEY_I);
    K(in, ACT_EXTERNAL_UP, GLFW_KEY_O);
    K(in, ACT_EXTERNAL_DN, GLFW_KEY_L);

    // Physics
    K(in, ACT_INVERT_TOGGLE,   GLFW_KEY_V);
    K(in, ACT_SENSORGAMMA_UP,  GLFW_KEY_X);
    K(in, ACT_SENSORGAMMA_DN,  GLFW_KEY_Z);
    K(in, ACT_SATKNEE_UP,      GLFW_KEY_8);
    K(in, ACT_SATKNEE_DN,      GLFW_KEY_7);
    K(in, ACT_COLORCROSS_UP,   GLFW_KEY_0);
    K(in, ACT_COLORCROSS_DN,   GLFW_KEY_9);

    // Thermal (numpad column)
    K(in, ACT_THERMAMP_UP,   GLFW_KEY_KP_4);
    K(in, ACT_THERMAMP_DN,   GLFW_KEY_KP_1);
    K(in, ACT_THERMSCALE_UP, GLFW_KEY_KP_5);
    K(in, ACT_THERMSCALE_DN, GLFW_KEY_KP_2);
    K(in, ACT_THERMSPEED_UP, GLFW_KEY_KP_6);
    K(in, ACT_THERMSPEED_DN, GLFW_KEY_KP_3);
    K(in, ACT_THERMRISE_UP,  GLFW_KEY_KP_8);
    K(in, ACT_THERMRISE_DN,  GLFW_KEY_KP_7);
    K(in, ACT_THERMSWIRL_UP, GLFW_KEY_KP_0);  // NP0 = swirl+ in old code
    K(in, ACT_THERMSWIRL_DN, GLFW_KEY_KP_9);  // NP9 = swirl-

    // Patterns / inject
    K(in, ACT_PATTERN_HBARS,   GLFW_KEY_1);
    K(in, ACT_PATTERN_VBARS,   GLFW_KEY_2);
    K(in, ACT_PATTERN_DOT,     GLFW_KEY_3);
    K(in, ACT_PATTERN_CHECKER, GLFW_KEY_4);
    K(in, ACT_PATTERN_GRAD,    GLFW_KEY_5);
    K(in, ACT_INJECT_HOLD,     GLFW_KEY_SPACE);

    // App
    K(in, ACT_QUIT,              GLFW_KEY_ESCAPE);
    K(in, ACT_CLEAR,             GLFW_KEY_C);
    K(in, ACT_PAUSE,             GLFW_KEY_P);                      // plain P
    K(in, ACT_HELP,              GLFW_KEY_H);
    K(in, ACT_RELOAD_SHADERS,    GLFW_KEY_BACKSLASH);
    K(in, ACT_FULLSCREEN,        GLFW_KEY_F11);
    K(in, ACT_REC_TOGGLE,        GLFW_KEY_GRAVE_ACCENT);
    K(in, ACT_SCREENSHOT,        GLFW_KEY_PRINT_SCREEN);
    K(in, ACT_PRESET_SAVE,       GLFW_KEY_S, GLFW_MOD_CONTROL);
    K(in, ACT_PRESET_NEXT,       GLFW_KEY_N, GLFW_MOD_CONTROL);
    K(in, ACT_PRESET_PREV,       GLFW_KEY_P, GLFW_MOD_CONTROL);
    K(in, ACT_BLURQ_CYCLE,       GLFW_KEY_PAGE_UP);
    K(in, ACT_CAQ_CYCLE,         GLFW_KEY_F12);
    K(in, ACT_NOISEQ_CYCLE,      GLFW_KEY_HOME);
    K(in, ACT_FIELDS_CYCLE,      GLFW_KEY_END);
    K(in, ACT_PRINT_HELP_STDOUT, GLFW_KEY_SLASH);  // '?' / shifted slash

#ifdef __APPLE__
    install_mac_keyboard_aliases(in, /*onlyIfMissing=*/false);
#endif

    // Help navigation (also consumes the main-panel arrow keys when the
    // help panel is open — we dispatch both from the same bindings and let
    // the handler decide which to act on based on help visibility).
    // No defaults for HELP_UP/DN/ENTER/BACK yet; those are wired in C3 so
    // we don't double-bind keys that already serve other purposes.

    // V-4 slot defaults (C4 wires handlers). Chords to keep keyboard unburdened.
    // Slot 1: F1..F10 are layer toggles, so we use a different chord: Alt+Q / Alt+A
    //   for cycle, Alt+W/S for param, Alt+Z off, Alt+X for B-source cycle.
    // Slot 2: Alt+Shift+same mirrors, but Alt+Shift is fiddly on some keymaps —
    //   use Ctrl+Alt+ pattern instead.
    K(in, ACT_VFX1_CYCLE_FWD,  GLFW_KEY_RIGHT_BRACKET, GLFW_MOD_ALT);
    K(in, ACT_VFX1_CYCLE_BACK, GLFW_KEY_LEFT_BRACKET,  GLFW_MOD_ALT);
    K(in, ACT_VFX1_OFF,        GLFW_KEY_BACKSLASH,     GLFW_MOD_ALT);
    K(in, ACT_VFX1_PARAM_UP,   GLFW_KEY_APOSTROPHE,    GLFW_MOD_ALT);
    K(in, ACT_VFX1_PARAM_DN,   GLFW_KEY_SEMICOLON,     GLFW_MOD_ALT);
    K(in, ACT_VFX1_BSRC_CYCLE, GLFW_KEY_SLASH,         GLFW_MOD_ALT);

    K(in, ACT_VFX2_CYCLE_FWD,  GLFW_KEY_RIGHT_BRACKET, GLFW_MOD_CONTROL | GLFW_MOD_ALT);
    K(in, ACT_VFX2_CYCLE_BACK, GLFW_KEY_LEFT_BRACKET,  GLFW_MOD_CONTROL | GLFW_MOD_ALT);
    K(in, ACT_VFX2_OFF,        GLFW_KEY_BACKSLASH,     GLFW_MOD_CONTROL | GLFW_MOD_ALT);
    K(in, ACT_VFX2_PARAM_UP,   GLFW_KEY_APOSTROPHE,    GLFW_MOD_CONTROL | GLFW_MOD_ALT);
    K(in, ACT_VFX2_PARAM_DN,   GLFW_KEY_SEMICOLON,     GLFW_MOD_CONTROL | GLFW_MOD_ALT);
    K(in, ACT_VFX2_BSRC_CYCLE, GLFW_KEY_SLASH,         GLFW_MOD_CONTROL | GLFW_MOD_ALT);

    // Output fade — Ctrl+Up/Down as keyboard fallback (gamepad right-stick in C2).
    K(in, ACT_OUTFADE_UP, GLFW_KEY_UP,   GLFW_MOD_CONTROL);
    K(in, ACT_OUTFADE_DN, GLFW_KEY_DOWN, GLFW_MOD_CONTROL);

    // BPM
    K(in, ACT_BPM_TAP,              GLFW_KEY_TAB);
    K(in, ACT_BPM_SYNC_TOGGLE,      GLFW_KEY_TAB,       GLFW_MOD_CONTROL);
    K(in, ACT_BPM_DIV_CYCLE,        GLFW_KEY_TAB,       GLFW_MOD_ALT);
    K(in, ACT_BPM_INJECT_TOGGLE,    GLFW_KEY_I,         GLFW_MOD_CONTROL | GLFW_MOD_ALT);
    K(in, ACT_BPM_STROBE_TOGGLE,    GLFW_KEY_S,         GLFW_MOD_CONTROL | GLFW_MOD_ALT);
    K(in, ACT_BPM_VFXCYCLE_TOGGLE,  GLFW_KEY_V,         GLFW_MOD_CONTROL | GLFW_MOD_ALT);
    K(in, ACT_BPM_FLASH_TOGGLE,     GLFW_KEY_F,         GLFW_MOD_CONTROL | GLFW_MOD_ALT);
    K(in, ACT_BPM_DECAYDIP_TOGGLE,  GLFW_KEY_D,         GLFW_MOD_CONTROL | GLFW_MOD_ALT);

#ifdef __APPLE__
    // DDJ-FLX2 performance defaults. These are inert unless a matching
    // CoreMIDI source is connected, and stay confined to Apple builds for
    // now while the macOS controller path is being proven out.
    midiPortHint_ = "DDJ-FLX2";

    // Jog wheels: Pioneer relative CC centered on 0x40.
    MIDI(in, ACT_THETA_AXIS,   SRC_MIDI_CC, 34, 1, 1.6f, false, true); // deck 1 platter
    MIDI(in, ACT_ZOOM_AXIS,    SRC_MIDI_CC, 34, 2, 1.8f, true,  true); // deck 2 platter
    MIDI(in, ACT_TRANS_X_AXIS, SRC_MIDI_CC, 33, 1, 1.4f, false, true); // left wheel side
    MIDI(in, ACT_TRANS_Y_AXIS, SRC_MIDI_CC, 33, 2, 1.4f, true,  true); // right wheel side

    // 14-bit mixer/fader controls. `delta` makes absolute hardware knobs
    // behave like parameter nudges, while the crossfader is absolute.
    MIDI(in, ACT_THETA_AXIS,   SRC_MIDI_CC14,  0, 1, 900.0f, false, false, true); // deck 1 tempo
    MIDI(in, ACT_SAT_UP,      SRC_MIDI_CC14,  7, 1, 260.0f, false, false, true);
    MIDI(in, ACT_HUE_UP,      SRC_MIDI_CC14, 11, 1, 420.0f, false, false, true);
    MIDI(in, ACT_GAMMA_UP,    SRC_MIDI_CC14, 15, 1, 130.0f, false, false, true);
    MIDI(in, ACT_OUTFADE_AXIS,SRC_MIDI_CC14,  7, 2,   1.0f, false, false, false,
         true, true); // deck 2 EQ HI: centered black/white output fade
    MIDI(in, ACT_BLURX_UP,    SRC_MIDI_CC14, 11, 2, 180.0f, false, false, true);
    MIDI(in, ACT_DECAY_UP,    SRC_MIDI_CC14, 15, 2, 130.0f, false, false, true);
    MIDI(in, ACT_ZOOM_AXIS,    SRC_MIDI_CC14,  0, 2, 700.0f, false, false, true); // deck 2 tempo
    MIDI(in, ACT_CONTRAST_UP,  SRC_MIDI_CC14,  8, 7, 180.0f, false, false, true); // master level
    MIDI(in, ACT_BLURY_UP,     SRC_MIDI_CC14, 13, 7, 180.0f, false, false, true); // phones level
    MIDI(in, ACT_SHAPE_COUNT_AXIS,SRC_MIDI_CC14, 23, 7,  1.0f, false, false, false,
         true, false); // CFX CH1: persistent shape count 1..16
    MIDI(in, ACT_COUPLE_UP,   SRC_MIDI_CC14, 24, 7, 140.0f, false, false, true);
    MIDI(in, ACT_EXTERNAL_UP, SRC_MIDI_CC14, 19, 1, 150.0f, false, false, true);
    MIDI(in, ACT_THERMAMP_UP, SRC_MIDI_CC14, 19, 2, 150.0f, false, false, true);
    MIDI(in, ACT_EXTERNAL_AXIS,SRC_MIDI_CC14, 31, 7,  1.0f, false, false, false,
         true, false); // crossfader: direct external blend 0..1

    // Transport / utility buttons.
    MIDI(in, ACT_PAUSE,           SRC_MIDI_NOTE, 11, 1);
    MIDI(in, ACT_PAUSE,           SRC_MIDI_NOTE, 11, 2);
    MIDI(in, ACT_INJECT_HOLD,     SRC_MIDI_NOTE, 12, 1);
    MIDI(in, ACT_CLEAR,           SRC_MIDI_NOTE, 12, 2);
    MIDI(in, ACT_LAYER_EXTERNAL,  SRC_MIDI_NOTE, 84, 1);  // CH cue deck 1
    MIDI(in, ACT_LAYER_THERMAL,   SRC_MIDI_NOTE, 84, 2);  // CH cue deck 2
    MIDI(in, ACT_BPM_TAP,         SRC_MIDI_NOTE, 88, 1);
    MIDI(in, ACT_BPM_TAP,         SRC_MIDI_NOTE, 88, 2);
    MIDI(in, ACT_BPM_SYNC_TOGGLE, SRC_MIDI_NOTE,  1, 7);  // SMART FADER
    MIDI(in, ACT_HELP,            SRC_MIDI_NOTE, 99, 7);  // master cue

    // Pads, normal mode: deck 1 still selects inject patterns, and pads 1-4
    // additionally hold persistent shape injections. Deck 2 toggles layers.
    MIDI(in, ACT_SHAPE_TRIANGLE_HOLD, SRC_MIDI_NOTE, 0,  8);
    MIDI(in, ACT_SHAPE_STAR_HOLD,     SRC_MIDI_NOTE, 1,  8);
    MIDI(in, ACT_SHAPE_CIRCLE_HOLD,   SRC_MIDI_NOTE, 2,  8);
    MIDI(in, ACT_SHAPE_SQUARE_HOLD,   SRC_MIDI_NOTE, 3,  8);
    MIDI(in, ACT_PATTERN_HBARS,     SRC_MIDI_NOTE, 0,  8);
    MIDI(in, ACT_PATTERN_VBARS,     SRC_MIDI_NOTE, 1,  8);
    MIDI(in, ACT_PATTERN_DOT,       SRC_MIDI_NOTE, 2,  8);
    MIDI(in, ACT_PATTERN_CHECKER,   SRC_MIDI_NOTE, 3,  8);
    MIDI(in, ACT_PATTERN_GRAD,      SRC_MIDI_NOTE, 4,  8);
    MIDI(in, ACT_VFX1_CYCLE_BACK,   SRC_MIDI_NOTE, 5,  8);
    MIDI(in, ACT_VFX1_CYCLE_FWD,    SRC_MIDI_NOTE, 6,  8);
    MIDI(in, ACT_VFX1_OFF,          SRC_MIDI_NOTE, 7,  8);
    MIDI(in, ACT_LAYER_WARP,       SRC_MIDI_NOTE, 0, 10);
    MIDI(in, ACT_LAYER_OPTICS,     SRC_MIDI_NOTE, 1, 10);
    MIDI(in, ACT_LAYER_COLOR,      SRC_MIDI_NOTE, 2, 10);
    MIDI(in, ACT_LAYER_DECAY,      SRC_MIDI_NOTE, 3, 10);
    MIDI(in, ACT_LAYER_NOISE,      SRC_MIDI_NOTE, 4, 10);
    MIDI(in, ACT_LAYER_COUPLE,     SRC_MIDI_NOTE, 5, 10);
    MIDI(in, ACT_LAYER_EXTERNAL,   SRC_MIDI_NOTE, 6, 10);
    MIDI(in, ACT_LAYER_INJECT,     SRC_MIDI_NOTE, 7, 10);

    // Shifted pads: left deck reveals the VFX quick-select bank, right deck
    // keeps fast layer/quality access. The official FLX2 table
    // advertises channels 9/11 for shifted pads, but observed hardware also
    // sends Shift as note 63 while pads stay on channels 8/10. Support both.
    MIDI(in, ACT_VFX1_PAD_0,       SRC_MIDI_NOTE, 0,  9);
    MIDI(in, ACT_VFX1_PAD_1,       SRC_MIDI_NOTE, 1,  9);
    MIDI(in, ACT_VFX1_PAD_2,       SRC_MIDI_NOTE, 2,  9);
    MIDI(in, ACT_VFX1_PAD_3,       SRC_MIDI_NOTE, 3,  9);
    MIDI(in, ACT_VFX1_PAD_4,       SRC_MIDI_NOTE, 4,  9);
    MIDI(in, ACT_VFX1_PAD_5,       SRC_MIDI_NOTE, 5,  9);
    MIDI(in, ACT_VFX1_PAD_6,       SRC_MIDI_NOTE, 6,  9);
    MIDI(in, ACT_VFX1_PAD_7,       SRC_MIDI_NOTE, 7,  9);
    MIDI(in, ACT_LAYER_PHYSICS,    SRC_MIDI_NOTE, 0, 11);
    MIDI(in, ACT_LAYER_THERMAL,    SRC_MIDI_NOTE, 1, 11);
    MIDI(in, ACT_BLURQ_CYCLE,      SRC_MIDI_NOTE, 2, 11);
    MIDI(in, ACT_CAQ_CYCLE,        SRC_MIDI_NOTE, 3, 11);
    MIDI(in, ACT_NOISEQ_CYCLE,     SRC_MIDI_NOTE, 4, 11);
    MIDI(in, ACT_FIELDS_CYCLE,     SRC_MIDI_NOTE, 5, 11);
    MIDI(in, ACT_BPM_FLASH_TOGGLE, SRC_MIDI_NOTE, 6, 11);
    MIDI(in, ACT_BPM_DECAYDIP_TOGGLE, SRC_MIDI_NOTE, 7, 11);

    MIDI(in, ACT_VFX1_PAD_0,       SRC_MIDI_NOTE, 0,  8, 1, false, false, false, false, false, true);
    MIDI(in, ACT_VFX1_PAD_1,       SRC_MIDI_NOTE, 1,  8, 1, false, false, false, false, false, true);
    MIDI(in, ACT_VFX1_PAD_2,       SRC_MIDI_NOTE, 2,  8, 1, false, false, false, false, false, true);
    MIDI(in, ACT_VFX1_PAD_3,       SRC_MIDI_NOTE, 3,  8, 1, false, false, false, false, false, true);
    MIDI(in, ACT_VFX1_PAD_4,       SRC_MIDI_NOTE, 4,  8, 1, false, false, false, false, false, true);
    MIDI(in, ACT_VFX1_PAD_5,       SRC_MIDI_NOTE, 5,  8, 1, false, false, false, false, false, true);
    MIDI(in, ACT_VFX1_PAD_6,       SRC_MIDI_NOTE, 6,  8, 1, false, false, false, false, false, true);
    MIDI(in, ACT_VFX1_PAD_7,       SRC_MIDI_NOTE, 7,  8, 1, false, false, false, false, false, true);
    MIDI(in, ACT_LAYER_PHYSICS,    SRC_MIDI_NOTE, 0, 10, 1, false, false, false, false, false, true);
    MIDI(in, ACT_LAYER_THERMAL,    SRC_MIDI_NOTE, 1, 10, 1, false, false, false, false, false, true);
    MIDI(in, ACT_BLURQ_CYCLE,      SRC_MIDI_NOTE, 2, 10, 1, false, false, false, false, false, true);
    MIDI(in, ACT_CAQ_CYCLE,        SRC_MIDI_NOTE, 3, 10, 1, false, false, false, false, false, true);
    MIDI(in, ACT_NOISEQ_CYCLE,     SRC_MIDI_NOTE, 4, 10, 1, false, false, false, false, false, true);
    MIDI(in, ACT_FIELDS_CYCLE,     SRC_MIDI_NOTE, 5, 10, 1, false, false, false, false, false, true);
    MIDI(in, ACT_BPM_FLASH_TOGGLE, SRC_MIDI_NOTE, 6, 10, 1, false, false, false, false, false, true);
    MIDI(in, ACT_BPM_DECAYDIP_TOGGLE, SRC_MIDI_NOTE, 7, 10, 1, false, false, false, false, false, true);
#endif

    // Help nav — arrow keys dedicated while help is open (the handler checks
    // help visibility and consumes these only then, so they do NOT conflict
    // with ACT_TRANS_* outside the help UI).
    K(in, ACT_HELP_UP,    GLFW_KEY_UP);
    K(in, ACT_HELP_DN,    GLFW_KEY_DOWN);
    K(in, ACT_HELP_ENTER, GLFW_KEY_ENTER);
    K(in, ACT_HELP_BACK,  GLFW_KEY_ESCAPE);

    // ── Gamepad defaults — per-section contextual map ──────────────
    //
    // The gamepad is ALWAYS operating in one context: menu (navigation),
    // or a specific section. Each section owns its own map covering the
    // free inputs (everything except D-pad U/D, A, B, Back — those are
    // reserved for navigation). Inputs that a section doesn't bind stay
    // idle in that context.
    //
    // CTX_ANY bindings are always active, regardless of context. We only
    // use that for the universal help toggle on the Back button.

    auto GB = [&](BindContext ctx, ActionId a, int code) {
        Binding b; b.action = a; b.source = SRC_GAMEPAD_BTN; b.code = code;
        b.context = ctx;
        in.bind(b);
    };
    auto GA = [&](BindContext ctx, ActionId a, int code, float scale,
                  bool invert = false, float dz = 0.08f, bool absolute = false) {
        Binding b; b.action = a; b.source = SRC_GAMEPAD_AXIS; b.code = code;
        b.scale = scale; b.invert = invert; b.deadzone = dz;
        b.absolute = absolute; b.context = ctx;
        in.bind(b);
    };

    // CTX_ANY — always-global: just help toggle. Pressed Back on the
    // controller opens the help panel when it's closed, closes it when
    // it's open (via the existing ACT_HELP toggle semantics).
    GB(CTX_ANY, ACT_HELP, GLFW_GAMEPAD_BUTTON_BACK);

    // CTX_MENU — help visible, menu view. Navigation only.
    GB(CTX_MENU, ACT_HELP_UP,    GLFW_GAMEPAD_BUTTON_DPAD_UP);
    GB(CTX_MENU, ACT_HELP_DN,    GLFW_GAMEPAD_BUTTON_DPAD_DOWN);
    GB(CTX_MENU, ACT_HELP_ENTER, GLFW_GAMEPAD_BUTTON_A);
    GB(CTX_MENU, ACT_HELP_BACK,  GLFW_GAMEPAD_BUTTON_B);

    // ── Section contexts — the interesting ones ───────────────────

    // While the help is open in a section view we ALSO want D-pad U/D
    // to scroll the section body and B to go back to menu. We bind
    // those under each section's context so they fire when that section
    // is active. (Same binding repeated across contexts is simpler than
    // a third "section-nav" context.)
    auto addScrollNav = [&](BindContext ctx) {
        GB(ctx, ACT_HELP_UP,   GLFW_GAMEPAD_BUTTON_DPAD_UP);
        GB(ctx, ACT_HELP_DN,   GLFW_GAMEPAD_BUTTON_DPAD_DOWN);
        GB(ctx, ACT_HELP_BACK, GLFW_GAMEPAD_BUTTON_B);
    };
    for (int s = CTX_SEC_STATUS; s < CTX__COUNT; s++)
        addScrollNav((BindContext)s);

    // Warp — translate on LS, rotation on RS-X, zoom on RS-Y.
    // Triggers give you an alternate zoom at higher rate for fast punch-ins.
    GA(CTX_SEC_WARP, ACT_TRANS_X_AXIS, GLFW_GAMEPAD_AXIS_LEFT_X,  1.0f);
    GA(CTX_SEC_WARP, ACT_TRANS_Y_AXIS, GLFW_GAMEPAD_AXIS_LEFT_Y,  1.0f);
    GA(CTX_SEC_WARP, ACT_THETA_AXIS,   GLFW_GAMEPAD_AXIS_RIGHT_X, 1.0f);
    GA(CTX_SEC_WARP, ACT_ZOOM_AXIS,    GLFW_GAMEPAD_AXIS_RIGHT_Y, 1.0f, /*inv=*/true);
    GA(CTX_SEC_WARP, ACT_ZOOM_AXIS,    GLFW_GAMEPAD_AXIS_LEFT_TRIGGER,  2.5f, /*inv=*/true, /*dz=*/-0.08f);
    GA(CTX_SEC_WARP, ACT_ZOOM_AXIS,    GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER, 2.5f, /*inv=*/false, /*dz=*/-0.08f);

    // Optics — blur XY on LS, chroma + angle on RS, LB/RB cycle quality.
    GA(CTX_SEC_OPTICS, ACT_BLURX_UP, GLFW_GAMEPAD_AXIS_LEFT_X,  1.0f);
    GA(CTX_SEC_OPTICS, ACT_BLURY_UP, GLFW_GAMEPAD_AXIS_LEFT_Y,  1.0f, /*inv=*/true);
    GA(CTX_SEC_OPTICS, ACT_BLURANG_UP, GLFW_GAMEPAD_AXIS_RIGHT_X, 1.0f);
    GA(CTX_SEC_OPTICS, ACT_CHROMA_UP,  GLFW_GAMEPAD_AXIS_RIGHT_Y, 1.0f, /*inv=*/true);
    GB(CTX_SEC_OPTICS, ACT_BLURQ_CYCLE, GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
    GB(CTX_SEC_OPTICS, ACT_CAQ_CYCLE,   GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);

    // Color — hue/sat on LS, contrast/gamma on RS, triggers push hueRate
    // harder for rainbow chase, Y cycles noise quality (tangential but free).
    GA(CTX_SEC_COLOR, ACT_HUE_UP,      GLFW_GAMEPAD_AXIS_LEFT_X,  1.0f);
    GA(CTX_SEC_COLOR, ACT_SAT_UP,      GLFW_GAMEPAD_AXIS_LEFT_Y,  1.0f, /*inv=*/true);
    GA(CTX_SEC_COLOR, ACT_CONTRAST_UP, GLFW_GAMEPAD_AXIS_RIGHT_X, 1.0f);
    GA(CTX_SEC_COLOR, ACT_GAMMA_UP,    GLFW_GAMEPAD_AXIS_RIGHT_Y, 1.0f, /*inv=*/true);
    GA(CTX_SEC_COLOR, ACT_HUE_UP,      GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER, 3.0f, /*inv=*/false, /*dz=*/-0.08f);
    GA(CTX_SEC_COLOR, ACT_HUE_DN,      GLFW_GAMEPAD_AXIS_LEFT_TRIGGER,  3.0f, /*inv=*/false, /*dz=*/-0.08f);

    // Dynamics — decay on RS-Y (the big knob), noise on RS-X, couple on
    // LS-X, external on LS-Y. Triggers are momentary-push actions.
    GA(CTX_SEC_DYN, ACT_COUPLE_UP,   GLFW_GAMEPAD_AXIS_LEFT_X,  1.0f);
    GA(CTX_SEC_DYN, ACT_EXTERNAL_UP, GLFW_GAMEPAD_AXIS_LEFT_Y,  1.0f, /*inv=*/true);
    GA(CTX_SEC_DYN, ACT_NOISE_UP,    GLFW_GAMEPAD_AXIS_RIGHT_X, 1.0f);
    GA(CTX_SEC_DYN, ACT_DECAY_UP,    GLFW_GAMEPAD_AXIS_RIGHT_Y, 1.0f, /*inv=*/true);
    GA(CTX_SEC_DYN, ACT_DECAY_DN,    GLFW_GAMEPAD_AXIS_LEFT_TRIGGER,  3.0f, /*inv=*/false, /*dz=*/-0.08f);
    GA(CTX_SEC_DYN, ACT_NOISE_UP,    GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER, 3.0f, /*inv=*/false, /*dz=*/-0.08f);

    // Physics — sensor gamma on LS-X, sat-knee on LS-Y, color-cross on RS-X.
    // Y = invert toggle. Triggers adjust sensor gamma at higher sensitivity.
    GA(CTX_SEC_PHYSICS, ACT_SENSORGAMMA_UP, GLFW_GAMEPAD_AXIS_LEFT_X,  1.0f);
    GA(CTX_SEC_PHYSICS, ACT_SATKNEE_UP,     GLFW_GAMEPAD_AXIS_LEFT_Y,  1.0f, /*inv=*/true);
    GA(CTX_SEC_PHYSICS, ACT_COLORCROSS_UP,  GLFW_GAMEPAD_AXIS_RIGHT_X, 1.0f);
    GB(CTX_SEC_PHYSICS, ACT_INVERT_TOGGLE,  GLFW_GAMEPAD_BUTTON_Y);
    GA(CTX_SEC_PHYSICS, ACT_SENSORGAMMA_DN, GLFW_GAMEPAD_AXIS_LEFT_TRIGGER,  3.0f, /*inv=*/false, /*dz=*/-0.08f);
    GA(CTX_SEC_PHYSICS, ACT_SENSORGAMMA_UP, GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER, 3.0f, /*inv=*/false, /*dz=*/-0.08f);

    // Thermal — scale on LS-X, amp on LS-Y, speed on RS-X, rise on RS-Y.
    // Triggers drive swirl (down/up). Plenty of axis surface for the 5
    // thermal knobs.
    GA(CTX_SEC_THERMAL, ACT_THERMSCALE_UP, GLFW_GAMEPAD_AXIS_LEFT_X,  1.0f);
    GA(CTX_SEC_THERMAL, ACT_THERMAMP_UP,   GLFW_GAMEPAD_AXIS_LEFT_Y,  1.0f, /*inv=*/true);
    GA(CTX_SEC_THERMAL, ACT_THERMSPEED_UP, GLFW_GAMEPAD_AXIS_RIGHT_X, 1.0f);
    GA(CTX_SEC_THERMAL, ACT_THERMRISE_UP,  GLFW_GAMEPAD_AXIS_RIGHT_Y, 1.0f, /*inv=*/true);
    GA(CTX_SEC_THERMAL, ACT_THERMSWIRL_DN, GLFW_GAMEPAD_AXIS_LEFT_TRIGGER,  2.0f, /*inv=*/false, /*dz=*/-0.08f);
    GA(CTX_SEC_THERMAL, ACT_THERMSWIRL_UP, GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER, 2.0f, /*inv=*/false, /*dz=*/-0.08f);

    // Inject — D-pad L/R steps through the 5 patterns (cursor = p.pattern),
    // triggers hold inject, X/Y/LB/RB give direct shortcuts to the common
    // patterns. A fires inject as a tap (press only) — useful for short
    // jabs without reaching for a trigger.
    GB(CTX_SEC_INJECT, ACT_PATTERN_CURSOR_UP, GLFW_GAMEPAD_BUTTON_DPAD_LEFT);
    GB(CTX_SEC_INJECT, ACT_PATTERN_CURSOR_DN, GLFW_GAMEPAD_BUTTON_DPAD_RIGHT);
    GB(CTX_SEC_INJECT, ACT_PATTERN_HBARS,   GLFW_GAMEPAD_BUTTON_X);
    GB(CTX_SEC_INJECT, ACT_PATTERN_DOT,     GLFW_GAMEPAD_BUTTON_Y);
    GB(CTX_SEC_INJECT, ACT_PATTERN_CHECKER, GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
    GB(CTX_SEC_INJECT, ACT_PATTERN_GRAD,    GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
    GB(CTX_SEC_INJECT, ACT_INJECT_HOLD,     GLFW_GAMEPAD_BUTTON_A);
    GA(CTX_SEC_INJECT, ACT_INJECT_HOLD,     GLFW_GAMEPAD_AXIS_LEFT_TRIGGER,  1.0f, /*inv=*/false, /*dz=*/-0.3f);
    GA(CTX_SEC_INJECT, ACT_INJECT_HOLD,     GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER, 1.0f, /*inv=*/false, /*dz=*/-0.3f);

    // VFX-1 — LB/RB and D-pad L/R both cycle the effect (D-pad doubles
    // for finer stepping through the 19-entry list); LT/RT param -/+;
    // X off; Y cycle B-source; LS-X as an alternate param axis.
    GB(CTX_SEC_VFX1, ACT_VFX1_CYCLE_BACK, GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
    GB(CTX_SEC_VFX1, ACT_VFX1_CYCLE_FWD,  GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
    GB(CTX_SEC_VFX1, ACT_VFX1_CYCLE_BACK, GLFW_GAMEPAD_BUTTON_DPAD_LEFT);
    GB(CTX_SEC_VFX1, ACT_VFX1_CYCLE_FWD,  GLFW_GAMEPAD_BUTTON_DPAD_RIGHT);
    GB(CTX_SEC_VFX1, ACT_VFX1_OFF,        GLFW_GAMEPAD_BUTTON_X);
    GB(CTX_SEC_VFX1, ACT_VFX1_BSRC_CYCLE, GLFW_GAMEPAD_BUTTON_Y);
    GA(CTX_SEC_VFX1, ACT_VFX1_PARAM_DN, GLFW_GAMEPAD_AXIS_LEFT_TRIGGER,  2.0f, /*inv=*/false, /*dz=*/-0.08f);
    GA(CTX_SEC_VFX1, ACT_VFX1_PARAM_UP, GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER, 2.0f, /*inv=*/false, /*dz=*/-0.08f);
    GA(CTX_SEC_VFX1, ACT_VFX1_PARAM_UP, GLFW_GAMEPAD_AXIS_LEFT_X, 1.0f);

    // VFX-2 — mirror of VFX-1.
    GB(CTX_SEC_VFX2, ACT_VFX2_CYCLE_BACK, GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
    GB(CTX_SEC_VFX2, ACT_VFX2_CYCLE_FWD,  GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
    GB(CTX_SEC_VFX2, ACT_VFX2_CYCLE_BACK, GLFW_GAMEPAD_BUTTON_DPAD_LEFT);
    GB(CTX_SEC_VFX2, ACT_VFX2_CYCLE_FWD,  GLFW_GAMEPAD_BUTTON_DPAD_RIGHT);
    GB(CTX_SEC_VFX2, ACT_VFX2_OFF,        GLFW_GAMEPAD_BUTTON_X);
    GB(CTX_SEC_VFX2, ACT_VFX2_BSRC_CYCLE, GLFW_GAMEPAD_BUTTON_Y);
    GA(CTX_SEC_VFX2, ACT_VFX2_PARAM_DN, GLFW_GAMEPAD_AXIS_LEFT_TRIGGER,  2.0f, /*inv=*/false, /*dz=*/-0.08f);
    GA(CTX_SEC_VFX2, ACT_VFX2_PARAM_UP, GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER, 2.0f, /*inv=*/false, /*dz=*/-0.08f);
    GA(CTX_SEC_VFX2, ACT_VFX2_PARAM_UP, GLFW_GAMEPAD_AXIS_LEFT_X, 1.0f);

    // Output — RS-Y absolute fade axis; LS-Y coarse rate-integrating
    // fallback; LB/RB nudge step.
    GA(CTX_SEC_OUTPUT, ACT_OUTFADE_AXIS, GLFW_GAMEPAD_AXIS_RIGHT_Y, 1.0f,
       /*invert=*/true, /*dz=*/0.08f, /*absolute=*/true);
    GA(CTX_SEC_OUTPUT, ACT_OUTFADE_UP,   GLFW_GAMEPAD_AXIS_LEFT_Y,  1.0f, /*inv=*/true);
    GB(CTX_SEC_OUTPUT, ACT_OUTFADE_DN,   GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
    GB(CTX_SEC_OUTPUT, ACT_OUTFADE_UP,   GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);

    // BPM — A = tap (only works when help closed OR in section view, not
    // in menu view where A is reserved for enter). X = cycle div, Y =
    // sync toggle. LB/LT shift each modulation toggle.
    GB(CTX_SEC_BPM, ACT_BPM_TAP,             GLFW_GAMEPAD_BUTTON_A);
    GB(CTX_SEC_BPM, ACT_BPM_DIV_CYCLE,       GLFW_GAMEPAD_BUTTON_X);
    GB(CTX_SEC_BPM, ACT_BPM_SYNC_TOGGLE,     GLFW_GAMEPAD_BUTTON_Y);
    GB(CTX_SEC_BPM, ACT_BPM_INJECT_TOGGLE,   GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
    GB(CTX_SEC_BPM, ACT_BPM_STROBE_TOGGLE,   GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
    GB(CTX_SEC_BPM, ACT_BPM_VFXCYCLE_TOGGLE, GLFW_GAMEPAD_BUTTON_START);
    GB(CTX_SEC_BPM, ACT_BPM_FLASH_TOGGLE,    GLFW_GAMEPAD_BUTTON_LEFT_THUMB);
    GB(CTX_SEC_BPM, ACT_BPM_DECAYDIP_TOGGLE, GLFW_GAMEPAD_BUTTON_RIGHT_THUMB);

    // Layers — cursor-based navigation. D-pad L/R moves the armed layer
    // cursor across all 12; A toggles whichever is armed. Plus a few
    // direct shortcuts for the workhorses so live performance doesn't
    // have to step through a menu every time. X/Y = warp/optics (the
    // most frequently nudged), LB/RB = external/inject for live pulls.
    GB(CTX_SEC_LAYERS, ACT_LAYER_CURSOR_UP,    GLFW_GAMEPAD_BUTTON_DPAD_LEFT);
    GB(CTX_SEC_LAYERS, ACT_LAYER_CURSOR_DN,    GLFW_GAMEPAD_BUTTON_DPAD_RIGHT);
    GB(CTX_SEC_LAYERS, ACT_LAYER_TOGGLE_ARMED, GLFW_GAMEPAD_BUTTON_A);
    GB(CTX_SEC_LAYERS, ACT_LAYER_WARP,         GLFW_GAMEPAD_BUTTON_X);
    GB(CTX_SEC_LAYERS, ACT_LAYER_OPTICS,       GLFW_GAMEPAD_BUTTON_Y);
    GB(CTX_SEC_LAYERS, ACT_LAYER_EXTERNAL,     GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
    GB(CTX_SEC_LAYERS, ACT_LAYER_INJECT,       GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
    GB(CTX_SEC_LAYERS, ACT_LAYER_COUPLE,       GLFW_GAMEPAD_BUTTON_LEFT_THUMB);
    GB(CTX_SEC_LAYERS, ACT_LAYER_PHYSICS,      GLFW_GAMEPAD_BUTTON_RIGHT_THUMB);
    GB(CTX_SEC_LAYERS, ACT_LAYER_THERMAL,      GLFW_GAMEPAD_BUTTON_START);

    // Quality — cursor pattern over the 4 items plus direct shortcuts.
    GB(CTX_SEC_QUALITY, ACT_QUALITY_CURSOR_UP,  GLFW_GAMEPAD_BUTTON_DPAD_LEFT);
    GB(CTX_SEC_QUALITY, ACT_QUALITY_CURSOR_DN,  GLFW_GAMEPAD_BUTTON_DPAD_RIGHT);
    GB(CTX_SEC_QUALITY, ACT_QUALITY_FIRE_ARMED, GLFW_GAMEPAD_BUTTON_A);
    GB(CTX_SEC_QUALITY, ACT_BLURQ_CYCLE,  GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
    GB(CTX_SEC_QUALITY, ACT_CAQ_CYCLE,    GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
    GB(CTX_SEC_QUALITY, ACT_NOISEQ_CYCLE, GLFW_GAMEPAD_BUTTON_X);
    GB(CTX_SEC_QUALITY, ACT_FIELDS_CYCLE, GLFW_GAMEPAD_BUTTON_Y);

    // App — preset navigation and common meta actions.
    GB(CTX_SEC_APP, ACT_CLEAR,       GLFW_GAMEPAD_BUTTON_X);
    GB(CTX_SEC_APP, ACT_PAUSE,       GLFW_GAMEPAD_BUTTON_Y);
    GB(CTX_SEC_APP, ACT_PRESET_PREV, GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
    GB(CTX_SEC_APP, ACT_PRESET_NEXT, GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
    GB(CTX_SEC_APP, ACT_PRESET_SAVE, GLFW_GAMEPAD_BUTTON_START);
    GB(CTX_SEC_APP, ACT_REC_TOGGLE,  GLFW_GAMEPAD_BUTTON_LEFT_THUMB);
    GB(CTX_SEC_APP, ACT_FULLSCREEN,  GLFW_GAMEPAD_BUTTON_RIGHT_THUMB);

    // Status / Bindings: info-only, no gamepad beyond the universal
    // scroll/back binds added above.
}

// ─────────────────────────────────────────────────────────────────────────
// GLFW key → action dispatch.
//
// Shift is NOT part of the modmask we match against — it's the coarse-step
// multiplier (1.0 normally, 5.0 with Shift). We strip Shift from `mods`
// before comparing, and feed the multiplier into the step magnitude.
//
// Binding order matters slightly: we fire the first match. Consider the
// duplicate (ACT_TRANS_UP,  UP) vs (ACT_HELP_UP, UP) — main decides which
// to honour based on help visibility, but both dispatches are called in
// order of registration.
// ─────────────────────────────────────────────────────────────────────────
void Input::onKey(int key, int /*scancode*/, int action, int mods) {
    const bool press   = (action == GLFW_PRESS);
    const bool repeat  = (action == GLFW_REPEAT);
    const bool release = (action == GLFW_RELEASE);
    if (!handler_) return;

    // Shift is always used as the coarse-step multiplier, not as a binding
    // modifier. Match on Ctrl / Alt / Super so macOS can use Command-based
    // aliases without changing the other platforms.
    const int matchMods = mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER);
    const float coarse  = (mods & GLFW_MOD_SHIFT) ? 20.0f : 1.0f;

    for (const Binding& b : bindings_) {
        if (b.source != SRC_KEY) continue;
        if (b.code != key) continue;
        if (b.modmask != matchMods) continue;

        const ActionInfo* info = action_info(b.action);
        if (!info) continue;

        const float sign = b.invert ? -1.0f : 1.0f;

        switch (info->kind) {
        case AK_STEP:
            if (press || repeat) {
                const float mag = b.scale * sign * coarse;
                handler_(b.action, mag);
            }
            break;
        case AK_DISCRETE:
            if (press) handler_(b.action, 1.0f);
            break;
        case AK_TRIGGER:
            if (press)   handler_(b.action, 1.0f);
            if (release) handler_(b.action, 0.0f);
            break;
        case AK_RATE:
            // Keyboard presses on RATE actions count as nudges equivalent
            // to one "unit" per press. Kept for completeness; no current
            // defaults use this path.
            if (press || repeat) handler_(b.action, b.scale * sign * coarse);
            break;
        }
        // Continue iterating — multiple bindings to the same (key,mods)
        // combo all fire. That's intentional so e.g. UP can drive both
        // warp translate and help cursor while the handler decides.
    }
}

// ─────────────────────────────────────────────────────────────────────────
// bindings.ini — very small INI parser. Example:
//   [keyboard]
//   warp.zoom+ = Q
//   warp.zoom- = A scale=0.5
//   preset.save = Ctrl+S
// Entries augment or override the defaults for the named action.
// ─────────────────────────────────────────────────────────────────────────

static std::string s_trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b-1])) b--;
    return s.substr(a, b - a);
}

// Parse a key spec like "Ctrl+Alt+S" or "F1" or "Left" into (glfwKey, mods).
// Returns true on success. Unknown names -> false.
static bool parse_key_spec(const std::string& spec, int& outKey, int& outMods) {
    outKey = 0; outMods = 0;
    std::string s = spec;
    // split on '+'
    std::vector<std::string> parts;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find('+', i);
        if (j == std::string::npos) { parts.push_back(s_trim(s.substr(i))); break; }
        parts.push_back(s_trim(s.substr(i, j - i)));
        i = j + 1;
    }
    if (parts.empty()) return false;

    auto ieq = [](const std::string& a, const char* b) {
        if (a.size() != std::strlen(b)) return false;
        for (size_t k = 0; k < a.size(); k++)
            if (std::tolower((unsigned char)a[k]) != std::tolower((unsigned char)b[k])) return false;
        return true;
    };

    for (size_t k = 0; k + 1 < parts.size(); k++) {
        const std::string& m = parts[k];
        if      (ieq(m, "Ctrl") || ieq(m, "Control")) outMods |= GLFW_MOD_CONTROL;
        else if (ieq(m, "Alt") || ieq(m, "Option"))   outMods |= GLFW_MOD_ALT;
        else if (ieq(m, "Shift"))                      outMods |= GLFW_MOD_SHIFT;
        else if (ieq(m, "Cmd") || ieq(m, "Command") || ieq(m, "Super"))
                                                         outMods |= GLFW_MOD_SUPER;
        else return false;
    }
    const std::string& key = parts.back();

    // Single-char keys: letters / digits / punctuation.
    if (key.size() == 1) {
        char c = std::toupper((unsigned char)key[0]);
        if (c >= 'A' && c <= 'Z') { outKey = GLFW_KEY_A + (c - 'A'); return true; }
        if (c >= '0' && c <= '9') { outKey = GLFW_KEY_0 + (c - '0'); return true; }
        switch (c) {
            case ' ':  outKey = GLFW_KEY_SPACE;        return true;
            case '[':  outKey = GLFW_KEY_LEFT_BRACKET; return true;
            case ']':  outKey = GLFW_KEY_RIGHT_BRACKET;return true;
            case ';':  outKey = GLFW_KEY_SEMICOLON;    return true;
            case '\'': outKey = GLFW_KEY_APOSTROPHE;   return true;
            case ',':  outKey = GLFW_KEY_COMMA;        return true;
            case '.':  outKey = GLFW_KEY_PERIOD;       return true;
            case '-':  outKey = GLFW_KEY_MINUS;        return true;
            case '=':  outKey = GLFW_KEY_EQUAL;        return true;
            case '/':  outKey = GLFW_KEY_SLASH;        return true;
            case '\\': outKey = GLFW_KEY_BACKSLASH;    return true;
            case '`':  outKey = GLFW_KEY_GRAVE_ACCENT; return true;
        }
        return false;
    }

    // Named keys.
    struct N { const char* name; int code; };
    static const N names[] = {
        {"Space",     GLFW_KEY_SPACE},
        {"Enter",     GLFW_KEY_ENTER},     {"Return", GLFW_KEY_ENTER},
        {"Escape",    GLFW_KEY_ESCAPE},    {"Esc",    GLFW_KEY_ESCAPE},
        {"Tab",       GLFW_KEY_TAB},
        {"Backspace", GLFW_KEY_BACKSPACE},
        {"Insert",    GLFW_KEY_INSERT},
        {"Delete",    GLFW_KEY_DELETE},
        {"Home",      GLFW_KEY_HOME},      {"End",    GLFW_KEY_END},
        {"PageUp",    GLFW_KEY_PAGE_UP},   {"PgUp",   GLFW_KEY_PAGE_UP},
        {"PageDown",  GLFW_KEY_PAGE_DOWN}, {"PgDn",   GLFW_KEY_PAGE_DOWN},
        {"Left",      GLFW_KEY_LEFT},      {"Right",  GLFW_KEY_RIGHT},
        {"Up",        GLFW_KEY_UP},        {"Down",   GLFW_KEY_DOWN},
        {"F1",GLFW_KEY_F1},{"F2",GLFW_KEY_F2},{"F3",GLFW_KEY_F3},{"F4",GLFW_KEY_F4},
        {"F5",GLFW_KEY_F5},{"F6",GLFW_KEY_F6},{"F7",GLFW_KEY_F7},{"F8",GLFW_KEY_F8},
        {"F9",GLFW_KEY_F9},{"F10",GLFW_KEY_F10},{"F11",GLFW_KEY_F11},{"F12",GLFW_KEY_F12},
        {"KP0",GLFW_KEY_KP_0},{"KP1",GLFW_KEY_KP_1},{"KP2",GLFW_KEY_KP_2},
        {"KP3",GLFW_KEY_KP_3},{"KP4",GLFW_KEY_KP_4},{"KP5",GLFW_KEY_KP_5},
        {"KP6",GLFW_KEY_KP_6},{"KP7",GLFW_KEY_KP_7},{"KP8",GLFW_KEY_KP_8},
        {"KP9",GLFW_KEY_KP_9},
        {"PrintScreen", GLFW_KEY_PRINT_SCREEN},
        {"PrtSc",       GLFW_KEY_PRINT_SCREEN},
    };
    for (auto& n : names) if (ieq(key, n.name)) { outKey = n.code; return true; }
    // Numeric fallback: <NNN> — lets key_spec_string's round-trip path work
    // for any key we haven't mapped to a name yet. Paranoid parse.
    if (key.size() >= 3 && key.front() == '<' && key.back() == '>') {
        int v = std::atoi(key.c_str() + 1);
        if (v > 0 && v <= GLFW_KEY_LAST) { outKey = v; return true; }
    }
    return false;
}

// Spec-string generator — inverse of parse_key_spec. Used by saveIni.
static std::string key_spec_string(int key, int mods) {
    std::string s;
    if (mods & GLFW_MOD_CONTROL) s += "Ctrl+";
    if (mods & GLFW_MOD_ALT)     s += "Alt+";
    if (mods & GLFW_MOD_SHIFT)   s += "Shift+";
#ifdef __APPLE__
    if (mods & GLFW_MOD_SUPER)   s += "Cmd+";
#else
    if (mods & GLFW_MOD_SUPER)   s += "Super+";
#endif
    // Reverse lookup.
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) { s += (char)('A' + (key - GLFW_KEY_A)); return s; }
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) { s += (char)('0' + (key - GLFW_KEY_0)); return s; }
    switch (key) {
        case GLFW_KEY_SPACE:         s += "Space"; return s;
        case GLFW_KEY_ENTER:         s += "Enter"; return s;
        case GLFW_KEY_ESCAPE:        s += "Esc"; return s;
        case GLFW_KEY_TAB:           s += "Tab"; return s;
        case GLFW_KEY_INSERT:        s += "Insert"; return s;
        case GLFW_KEY_DELETE:        s += "Delete"; return s;
        case GLFW_KEY_HOME:          s += "Home"; return s;
        case GLFW_KEY_END:           s += "End"; return s;
        case GLFW_KEY_PAGE_UP:       s += "PgUp"; return s;
        case GLFW_KEY_PAGE_DOWN:     s += "PgDn"; return s;
        case GLFW_KEY_LEFT:          s += "Left"; return s;
        case GLFW_KEY_RIGHT:         s += "Right"; return s;
        case GLFW_KEY_UP:            s += "Up"; return s;
        case GLFW_KEY_DOWN:          s += "Down"; return s;
        case GLFW_KEY_LEFT_BRACKET:  s += "["; return s;
        case GLFW_KEY_RIGHT_BRACKET: s += "]"; return s;
        case GLFW_KEY_SEMICOLON:     s += ";"; return s;
        case GLFW_KEY_APOSTROPHE:    s += "'"; return s;
        case GLFW_KEY_COMMA:         s += ","; return s;
        case GLFW_KEY_PERIOD:        s += "."; return s;
        case GLFW_KEY_MINUS:         s += "-"; return s;
        case GLFW_KEY_EQUAL:         s += "="; return s;
        case GLFW_KEY_SLASH:         s += "/"; return s;
        case GLFW_KEY_BACKSLASH:     s += "\\"; return s;
        case GLFW_KEY_GRAVE_ACCENT:  s += "`"; return s;
        case GLFW_KEY_PRINT_SCREEN:  s += "PrtSc"; return s;
    }
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) {
        char buf[8]; std::snprintf(buf, sizeof buf, "F%d", 1 + (key - GLFW_KEY_F1));
        s += buf; return s;
    }
    if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9) {
        char buf[8]; std::snprintf(buf, sizeof buf, "KP%d", (key - GLFW_KEY_KP_0));
        s += buf; return s;
    }
    char buf[24]; std::snprintf(buf, sizeof buf, "<%d>", key);
    s += buf;
    return s;
}

bool Input::loadIni(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    char line[512];
    std::string section;

    while (std::fgets(line, sizeof line, f)) {
        std::string s = s_trim(line);
        if (s.empty() || s[0] == '#' || s[0] == ';') continue;
        if (s.front() == '[' && s.back() == ']') {
            section = s_trim(s.substr(1, s.size() - 2));
            continue;
        }
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = s_trim(s.substr(0, eq));
        std::string v = s_trim(s.substr(eq + 1));
        // strip trailing "# comment"
        size_t hash = v.find('#');
        if (hash != std::string::npos) v = s_trim(v.substr(0, hash));

        if (section == "midi" && k == "port") {
            setMidiPortHint(v);
            continue;
        }
        if (section == "midi" && k == "learn") {
            std::string lo = v;
            for (char& c : lo) c = (char)std::tolower((unsigned char)c);
            midiLearn_ = (lo == "1" || lo == "yes" || lo == "true" || lo == "on");
            continue;
        }
        if (section == "midi" && k == "clock") {
            continue; // reserved; current macOS pass records clock but does not own BPM.
        }

        const ActionInfo* info = action_info_by_name(k.c_str());
        if (!info) {
            std::fprintf(stderr, "[bindings] unknown action '%s' — skipped\n", k.c_str());
            continue;
        }

        // Optional "scale=X invert" suffix after the key name, space-separated.
        std::string keyPart = v;
        float scale = 1.0f;
        bool invert = false;
        float deadzone = 0.0f;
        int midiChannel = 0;

        // Split on whitespace.
        std::vector<std::string> toks;
        size_t i = 0;
        while (i < v.size()) {
            while (i < v.size() && std::isspace((unsigned char)v[i])) i++;
            size_t j = i;
            while (j < v.size() && !std::isspace((unsigned char)v[j])) j++;
            if (j > i) toks.push_back(v.substr(i, j - i));
            i = j;
        }
        if (toks.empty()) continue;
        keyPart = toks[0];
        bool absolute = false;
        bool relative = false;
        bool delta = false;
        bool bipolar = false;
        bool shifted = false;
        BindContext ctx = CTX_ANY;
        auto parse_ctx = [](const std::string& s) -> BindContext {
            // Lowercase, match section tags. "any", "menu", then the
            // section short names in HELP_SECTIONS order.
            std::string lo = s;
            for (auto& c : lo) c = (char)std::tolower((unsigned char)c);
            if (lo == "any")      return CTX_ANY;
            if (lo == "menu")     return CTX_MENU;
            if (lo == "status")   return CTX_SEC_STATUS;
            if (lo == "layers")   return CTX_SEC_LAYERS;
            if (lo == "warp")     return CTX_SEC_WARP;
            if (lo == "optics")   return CTX_SEC_OPTICS;
            if (lo == "color")    return CTX_SEC_COLOR;
            if (lo == "dyn" || lo == "dynamics") return CTX_SEC_DYN;
            if (lo == "physics")  return CTX_SEC_PHYSICS;
            if (lo == "thermal")  return CTX_SEC_THERMAL;
            if (lo == "inject")   return CTX_SEC_INJECT;
            if (lo == "vfx1")     return CTX_SEC_VFX1;
            if (lo == "vfx2")     return CTX_SEC_VFX2;
            if (lo == "output")   return CTX_SEC_OUTPUT;
            if (lo == "bpm")      return CTX_SEC_BPM;
            if (lo == "quality")  return CTX_SEC_QUALITY;
            if (lo == "app")      return CTX_SEC_APP;
            if (lo == "bindings") return CTX_SEC_BINDINGS;
            return CTX_ANY;
        };
        for (size_t t = 1; t < toks.size(); t++) {
            const std::string& opt = toks[t];
            if (opt.rfind("scale=", 0) == 0)      scale = (float)std::atof(opt.c_str() + 6);
            else if (opt == "invert")              invert = true;
            else if (opt == "abs" || opt == "absolute") absolute = true;
            else if (opt == "rel" || opt == "relative" || opt == "mode=relative") relative = true;
            else if (opt == "delta" || opt == "mode=delta") delta = true;
            else if (opt == "bipolar" || opt == "mode=bipolar") bipolar = true;
            else if (opt == "shift" || opt == "shifted") shifted = true;
            else if (opt.rfind("deadzone=", 0) == 0) deadzone = (float)std::atof(opt.c_str() + 9);
            else if (opt.rfind("ctx=", 0) == 0)   ctx = parse_ctx(opt.substr(4));
            else if (opt.rfind("ch=", 0) == 0)    midiChannel = std::atoi(opt.c_str() + 3);
        }
        if (midiChannel < 0 || midiChannel > 16) midiChannel = 0;

        Binding b;
        b.action   = info->id;
        b.scale    = scale;
        b.invert   = invert;
        b.deadzone = deadzone;
        b.absolute = absolute;
        b.relative = relative;
        b.delta    = delta;
        b.bipolar  = bipolar;
        b.shifted  = shifted;
        b.context  = ctx;

        if (section == "keyboard" || section.empty()) {
            int code = 0, mods = 0;
            if (!parse_key_spec(keyPart, code, mods)) {
                std::fprintf(stderr, "[bindings] cannot parse key '%s' for %s\n",
                             keyPart.c_str(), k.c_str());
                continue;
            }
            b.source = SRC_KEY;
            b.code   = code;
            b.modmask= mods;
        } else if (section == "gamepad") {
            // C2 will expand this. For now accept but don't dispatch.
            if (keyPart.rfind("btn:", 0) == 0) {
                b.source = SRC_GAMEPAD_BTN;
                b.code   = std::atoi(keyPart.c_str() + 4);
            } else if (keyPart.rfind("axis:", 0) == 0) {
                b.source = SRC_GAMEPAD_AXIS;
                b.code   = std::atoi(keyPart.c_str() + 5);
            } else {
                continue;
            }
        } else if (section == "midi") {
            if (keyPart.rfind("cc:", 0) == 0) {
                b.source  = SRC_MIDI_CC;
                b.code    = std::atoi(keyPart.c_str() + 3);
                b.modmask = midiChannel;
            } else if (keyPart.rfind("cc14:", 0) == 0) {
                b.source  = SRC_MIDI_CC14;
                b.code    = std::atoi(keyPart.c_str() + 5);
                b.modmask = midiChannel;
            } else if (keyPart.rfind("note:", 0) == 0) {
                b.source  = SRC_MIDI_NOTE;
                b.code    = std::atoi(keyPart.c_str() + 5);
                b.modmask = midiChannel;
            } else {
                continue;
            }
        } else {
            continue;  // unknown section
        }

        // File entries OVERRIDE prior default entries that share the
        // same (action, source, context). Different contexts of the same
        // action coexist so an INI tweak to e.g. ctx=warp doesn't nuke
        // the ctx=optics binding.
        bindings_.erase(std::remove_if(bindings_.begin(), bindings_.end(),
            [&](const Binding& x) {
                if (x.action != b.action || x.source != b.source || x.context != b.context)
                    return false;
                if (b.source == SRC_MIDI_CC || b.source == SRC_MIDI_CC14 || b.source == SRC_MIDI_NOTE)
                    return x.code == b.code && x.modmask == b.modmask
                        && x.shifted == b.shifted;
                return true;
            }), bindings_.end());
        bindings_.push_back(b);
    }
    std::fclose(f);
#ifdef __APPLE__
    const int added = install_mac_keyboard_aliases(*this, /*onlyIfMissing=*/true);
    if (added > 0) {
        std::printf("[bindings] added %d macOS keyboard aliases on top of %s\n",
                    added, path.c_str());
    }
#endif
    return true;
}

// ─────────────────────────────────────────────────────────────────────────
// Gamepad polling — GLFW's glfwGetGamepadState maps any SDL-known pad
// onto the standard 15-button / 6-axis layout. We remember the previous
// frame's buttons to fire DISCRETE/TRIGGER actions only on edges, and
// we integrate RATE actions per-frame.
//
// Triggers (LT/RT) rest at -1 and travel to +1. A binding with
// deadzone < 0 flags this: rather than a symmetric ±deadzone window,
// we treat the axis as "value shifted by +1 then /2" → 0..1, and
// apply a single-sided threshold. Makes triggers feel like buttons
// when you want them to.
// ─────────────────────────────────────────────────────────────────────────

static struct {
    bool btnPrev[GLFW_GAMEPAD_BUTTON_LAST + 1] = {};
    bool init = false;
} s_pad;

void Input::pollGamepad(int jid, float dt, BindContext currentCtx) {
    if (!handler_) return;
    if (!glfwJoystickPresent(jid)) { s_pad.init = false; return; }
    GLFWgamepadstate gp;
    if (!glfwGetGamepadState(jid, &gp)) { s_pad.init = false; return; }
    s_pad.init = true;

    auto ctxMatches = [&](const Binding& b) {
        return b.context == CTX_ANY || b.context == currentCtx;
    };

    // Button edges
    for (int i = 0; i <= GLFW_GAMEPAD_BUTTON_LAST; i++) {
        bool down = gp.buttons[i] == GLFW_PRESS;
        bool prev = s_pad.btnPrev[i];
        bool pressed  = down && !prev;
        bool released = !down && prev;
        s_pad.btnPrev[i] = down;
        if (!pressed && !released) continue;

        for (const Binding& b : bindings_) {
            if (b.source != SRC_GAMEPAD_BTN || b.code != i) continue;
            if (!ctxMatches(b)) continue;
            const ActionInfo* info = action_info(b.action);
            if (!info) continue;
            switch (info->kind) {
            case AK_DISCRETE: if (pressed) handler_(b.action, 1.0f); break;
            case AK_TRIGGER:
                if (pressed)  handler_(b.action, 1.0f);
                if (released) handler_(b.action, 0.0f);
                break;
            case AK_STEP:
                if (pressed) handler_(b.action, b.invert ? -b.scale : b.scale);
                break;
            case AK_RATE:
                // A button on a rate action = keyboard-like tick-per-press.
                if (pressed) handler_(b.action, b.invert ? -b.scale : b.scale);
                break;
            }
        }
    }

    // Axes (continuous): dispatch each frame. GLFW gives you -1..+1;
    // triggers rest at -1 at rest and go to +1 fully pressed.
    for (const Binding& b : bindings_) {
        if (b.source != SRC_GAMEPAD_AXIS) continue;
        if (b.code < 0 || b.code > GLFW_GAMEPAD_AXIS_LAST) continue;
        if (!ctxMatches(b)) continue;
        float v = gp.axes[b.code];

        // Trigger convention: deadzone < 0 indicates a "trigger axis"
        // binding — normalize from -1..+1 to 0..+1 and apply |dz| as a
        // single-sided gate.
        if (b.deadzone < 0.0f) {
            v = (v + 1.0f) * 0.5f;           // 0..1
            float gate = -b.deadzone;
            if (v < gate) v = 0.0f;
            else          v = (v - gate) / (1.0f - gate);
        } else {
            // Symmetric deadzone around 0.
            if (v >  b.deadzone) v = (v - b.deadzone) / (1.0f - b.deadzone);
            else if (v < -b.deadzone) v = (v + b.deadzone) / (1.0f - b.deadzone);
            else v = 0.0f;
        }

        if (b.invert) v = -v;

        const ActionInfo* info = action_info(b.action);
        if (!info) continue;

        // Two flavours of axis dispatch:
        //  absolute = true  → "the stick IS the knob". Dispatch `v * scale`
        //                     as-is. Self-centering. Good for output fade.
        //  absolute = false → integrating. Dispatch `v * scale * (dt*60)`
        //                     so full deflection ≈ 1 unit/frame at 60fps,
        //                     similar to a held keyboard key's auto-repeat.
        float out = b.absolute ? (v * b.scale)
                               : (v * b.scale * dt * 60.0f);
        if (info->kind == AK_RATE || info->kind == AK_STEP) {
            // absolute dispatches even when 0 so the parameter tracks the
            // stick all the way back to rest; integrating skips the zeros.
            if (b.absolute || out != 0.0f) handler_(b.action, out);
        }
    }
}

namespace {
struct MidiRuntime {
    bool opened = false;
    bool prevCcInit[17][128] = {};
    float prevCc[17][128] = {};
    bool ccSeen[17][128] = {};
    uint8_t ccVal[17][128] = {};
    bool deckShift[3] = {};
    double clockTimes[24] = {};
    int clockHead = 0;
    int clockCount = 0;
    double lastClockT = 0.0;
};
MidiRuntime g_midiRt;

static float midi_relative_delta(int value) {
    value &= 0x7F;
    if (value == 0x40 || value == 0) return 0.0f;
    if (value > 0x40) return (float)(value - 0x40);
    return (float)(value - 0x40);
}
}

void Input::pollMidi(float /*dt*/) {
    const double now = glfwGetTime();

#ifdef __APPLE__
    if (!g_midiRt.opened) {
        g_midiRt.opened = feedback_midi_open(midiPortHint_.c_str()) != 0;
    }

    char portName[256] = {};
    int connected = 0;
    feedback_midi_status(portName, (int)sizeof portName, &connected);
    midi_.connected = connected != 0;
    midi_.portName = midi_.connected ? portName : std::string();

    FeedbackMidiMsg msgs[512];
    int nmsg = feedback_midi_poll(msgs, 512);
#else
    FeedbackMidiMsg msgs[1];
    int nmsg = 0;
    midi_.connected = false;
    midi_.portName.clear();
#endif

    auto dispatch_note = [&](int ch, int note, int vel, bool on) {
        if (note == 63 && (ch == 1 || ch == 2)) {
            g_midiRt.deckShift[ch] = on;
            midi_.deck1Shift = g_midiRt.deckShift[1];
            midi_.deck2Shift = g_midiRt.deckShift[2];
        }
        const bool softwareShifted =
            (ch == 8 && g_midiRt.deckShift[1]) ||
            (ch == 10 && g_midiRt.deckShift[2]);

        midi_.lastNoteCh = ch;
        midi_.lastNoteNum = note;
        midi_.lastNoteVel = on ? vel : 0;
        if (midiLearn_) {
            std::printf("[midi-learn] ch=%d note:%d vel=%d %s\n",
                        ch, note, vel, on ? "on" : "off");
        }
        if (!handler_) return;
        for (const Binding& b : bindings_) {
            if (b.source != SRC_MIDI_NOTE) continue;
            if (b.code != note) continue;
            if (b.modmask != 0 && b.modmask != ch) continue;
            if (b.shifted != softwareShifted) continue;
            const ActionInfo* info = action_info(b.action);
            if (!info) continue;
            float mg = (vel / 127.0f) * b.scale;
            if (b.invert) mg = -mg;
            switch (info->kind) {
            case AK_DISCRETE: if (on) handler_(b.action, 1.0f); break;
            case AK_TRIGGER:
                if (on) handler_(b.action, 1.0f);
                else    handler_(b.action, 0.0f);
                break;
            case AK_STEP:
            case AK_RATE:
                if (on) handler_(b.action, mg);
                break;
            }
        }
    };

    auto dispatch_cc_value = [&](const Binding& b, int ch, int ccNum,
                                 int ccVal, float norm) {
        const ActionInfo* info = action_info(b.action);
        if (!info) return;

        float mg = norm;
        if (b.relative) {
            mg = midi_relative_delta(ccVal);
        } else if (b.delta) {
            if (!g_midiRt.prevCcInit[ch][b.code]) {
                g_midiRt.prevCcInit[ch][b.code] = true;
                g_midiRt.prevCc[ch][b.code] = norm;
                return;
            }
            mg = norm - g_midiRt.prevCc[ch][b.code];
            g_midiRt.prevCc[ch][b.code] = norm;
        } else if (b.bipolar) {
            mg = norm * 2.0f - 1.0f;
        }

        if (b.invert) mg = -mg;
        mg *= b.scale;

        switch (info->kind) {
        case AK_RATE:
        case AK_STEP:
            if (mg != 0.0f || b.absolute) handler_(b.action, mg);
            break;
        case AK_DISCRETE:
            if (ccVal > 63) handler_(b.action, 1.0f);
            break;
        case AK_TRIGGER:
            handler_(b.action, ccVal > 63 ? 1.0f : 0.0f);
            break;
        }
        (void)ccNum;
    };

    auto dispatch_cc = [&](int ch, int ccNum, int ccVal) {
        midi_.lastCcCh = ch;
        midi_.lastCcNum = ccNum;
        midi_.lastCcVal = ccVal;
        g_midiRt.ccSeen[ch][ccNum] = true;
        g_midiRt.ccVal[ch][ccNum] = (uint8_t)ccVal;

        if (midiLearn_) {
            std::printf("[midi-learn] ch=%d cc:%d val=%d\n", ch, ccNum, ccVal);
        }
        if (!handler_) return;

        for (const Binding& b : bindings_) {
            if (b.modmask != 0 && b.modmask != ch) continue;
            if (b.source == SRC_MIDI_CC) {
                if (b.code != ccNum) continue;
                dispatch_cc_value(b, ch, ccNum, ccVal, ccVal / 127.0f);
            } else if (b.source == SRC_MIDI_CC14) {
                if (b.code != ccNum && b.code + 32 != ccNum) continue;
                int msbCc = b.code;
                int lsbCc = b.code + 32;
                if (lsbCc > 127 || !g_midiRt.ccSeen[ch][msbCc]) continue;
                int msb = g_midiRt.ccVal[ch][msbCc] & 0x7F;
                int lsb = g_midiRt.ccSeen[ch][lsbCc] ? (g_midiRt.ccVal[ch][lsbCc] & 0x7F) : 0;
                int value14 = (msb << 7) | lsb;
                int displayVal = value14 >> 7;
                dispatch_cc_value(b, ch, msbCc, displayVal, value14 / 16383.0f);
            }
        }
    };

    for (int i = 0; i < nmsg; i++) {
        const FeedbackMidiMsg& m = msgs[i];
        if (m.len == 0) continue;
        uint8_t b0 = m.b[0], b1 = m.b[1], b2 = m.b[2];

        if (b0 >= 0xF8) {
            if (b0 == 0xF8) {
                if (g_midiRt.lastClockT > 0.0) {
                    double gap = now - g_midiRt.lastClockT;
                    g_midiRt.clockTimes[g_midiRt.clockHead] = gap;
                    g_midiRt.clockHead = (g_midiRt.clockHead + 1) % 24;
                    if (g_midiRt.clockCount < 24) g_midiRt.clockCount++;
                    if (g_midiRt.clockCount >= 4) {
                        double sum = 0.0;
                        for (int k = 0; k < g_midiRt.clockCount; k++) sum += g_midiRt.clockTimes[k];
                        double avg = sum / g_midiRt.clockCount;
                        if (avg > 1e-5) {
                            float bpm = (float)(60.0 / (avg * 24.0));
                            if (bpm >= 20.f && bpm <= 400.f) midi_.derivedBpm = bpm;
                        }
                    }
                }
                g_midiRt.lastClockT = now;
                midi_.clockLive = true;
            } else if (b0 == 0xFA) {
                midi_.startPending = true;
                g_midiRt.clockCount = 0;
                g_midiRt.clockHead = 0;
                g_midiRt.lastClockT = 0.0;
            } else if (b0 == 0xFC) {
                midi_.stopPending = true;
            }
            if (midiLearn_) std::printf("[midi-learn] realtime 0x%02X\n", b0);
            continue;
        }

        uint8_t type = b0 & 0xF0;
        int ch = (b0 & 0x0F) + 1;
        if (type == 0x90 || type == 0x80) {
            int note = b1 & 0x7F;
            int vel = b2 & 0x7F;
            bool on = (type == 0x90) && vel > 0;
            dispatch_note(ch, note, vel, on);
        } else if (type == 0xB0) {
            dispatch_cc(ch, b1 & 0x7F, b2 & 0x7F);
        }
    }

    if (midi_.clockLive && g_midiRt.lastClockT > 0.0 && (now - g_midiRt.lastClockT) > 0.5) {
        midi_.clockLive = false;
        g_midiRt.clockCount = 0;
    }
}

bool Input::sendMidiNote(int channel, int note, int velocity) {
#ifdef __APPLE__
    if (channel < 1 || channel > 16 || note < 0 || note > 127) return false;
    if (velocity < 0) velocity = 0;
    if (velocity > 127) velocity = 127;
    return feedback_midi_send_note(channel, note, velocity) != 0;
#else
    (void)channel; (void)note; (void)velocity;
    return false;
#endif
}

bool Input::saveIni(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;

    std::fprintf(f,
"# bindings.ini — input map for feedback.exe\n"
"#\n"
"# Sections: [keyboard]  [gamepad]  [midi]\n"
"# Syntax:   action.name = KEY [scale=X] [invert] [deadzone=X]\n"
"# Examples: warp.zoom+ = Q\n"
"#           warp.zoom+ = Q scale=0.5\n"
"#           preset.save = Ctrl+S\n"
"#           outfade.axis = axis:4 scale=1.0 deadzone=0.08 abs\n"
"#\n"
"# Options:\n"
"#   scale=X    multiplier applied before dispatch\n"
"#   invert     negate value before dispatch\n"
"#   deadzone=X axis deadzone (symmetric). Negative = trigger mode.\n"
"#   abs        dispatch axis position directly (no dt integration).\n"
"#              Natural for 'the stick IS the knob' mappings. Sticks\n"
"#              self-center, so letting go returns the parameter to 0.\n"
"#   relative   MIDI CC is 0x40-centered relative motion (jog wheels).\n"
"#   delta      MIDI CC/CC14 dispatches change since previous value.\n"
"#   bipolar    MIDI absolute value 0..1 is remapped to -1..+1.\n"
"#   shifted    MIDI note requires DDJ-FLX2 Shift note held.\n"
"#\n"
"# Modifier prefixes: Ctrl+ / Alt+ / Shift+ / Cmd+ (or Super+)\n"
"# (Shift is reserved as the coarse-step multiplier — using it as a modifier\n"
"#  is accepted but it only matters if you want to create a distinct binding\n"
"#  from the bare key.)\n"
"#\n"
"# Run the app and press H to see what each action does in the drill-down\n"
"# panel.  Unknown action names are skipped with a warning to stderr.\n"
"\n");

    auto dump_section = [&](const char* name, BindSource src, bool emitHeader = true) {
        if (emitHeader) {
        std::fprintf(f, "[%s]\n", name);
        }
        for (int i = 0; i < action_info_count(); i++) {
            ActionId id = ACTIONS[i].id;
            for (const Binding& b : bindings_) {
                if (b.action != id || b.source != src) continue;
                if (src == SRC_KEY) {
                    std::fprintf(f, "%-24s = %s", ACTIONS[i].name,
                                 key_spec_string(b.code, b.modmask).c_str());
                } else if (src == SRC_GAMEPAD_BTN) {
                    std::fprintf(f, "%-24s = btn:%d", ACTIONS[i].name, b.code);
                } else if (src == SRC_GAMEPAD_AXIS) {
                    std::fprintf(f, "%-24s = axis:%d", ACTIONS[i].name, b.code);
                } else if (src == SRC_MIDI_CC) {
                    std::fprintf(f, "%-24s = cc:%d", ACTIONS[i].name, b.code);
                } else if (src == SRC_MIDI_CC14) {
                    std::fprintf(f, "%-24s = cc14:%d", ACTIONS[i].name, b.code);
                } else if (src == SRC_MIDI_NOTE) {
                    std::fprintf(f, "%-24s = note:%d", ACTIONS[i].name, b.code);
                }
                if ((src == SRC_MIDI_CC || src == SRC_MIDI_CC14 || src == SRC_MIDI_NOTE)
                    && b.modmask != 0) {
                    std::fprintf(f, " ch=%d", b.modmask);
                }
                if (b.scale != 1.0f)  std::fprintf(f, " scale=%.3f", b.scale);
                if (b.invert)         std::fprintf(f, " invert");
                if (b.deadzone != 0.0f) std::fprintf(f, " deadzone=%.3f", b.deadzone);
                if (b.absolute)       std::fprintf(f, " abs");
                if (b.relative)       std::fprintf(f, " relative");
                if (b.delta)          std::fprintf(f, " delta");
                if (b.bipolar)        std::fprintf(f, " bipolar");
                if (b.shifted)        std::fprintf(f, " shifted");
                if (b.context != CTX_ANY) {
                    static const char* CTXN[] = {
                        "any","menu","status","layers","warp","optics",
                        "color","dyn","physics","thermal","inject",
                        "vfx1","vfx2","output","bpm","quality","app","bindings"
                    };
                    if (b.context >= 0 && b.context < CTX__COUNT)
                        std::fprintf(f, " ctx=%s", CTXN[b.context]);
                }
                std::fprintf(f, "\n");
            }
        }
        std::fprintf(f, "\n");
    };

    dump_section("keyboard", SRC_KEY);
    dump_section("gamepad",  SRC_GAMEPAD_BTN);
    dump_section("gamepad",  SRC_GAMEPAD_AXIS);

    std::fprintf(f,
"[midi]\n"
"# macOS: CoreMIDI opens the first source whose name contains this string.\n"
"# The default Apple build targets AlphaTheta/Pioneer DDJ-FLX2.\n"
"port = %s\n"
"# learn = off    # set on, or launch with --midi-learn, to print incoming messages\n"
"#\n"
"# MIDI binding syntax:\n"
"#   action.name = note:NN [ch=N]\n"
"#   action.name = cc:NN [ch=N] [relative|delta|bipolar]\n"
"#   action.name = cc14:NN [ch=N] [delta|bipolar]\n"
"#   Add 'shifted' to require the DDJ-FLX2 Shift button for pad notes.\n"
"# ch=0 or omitted matches any channel.\n"
"\n",
        midiPortHint_.empty() ? "DDJ-FLX2" : midiPortHint_.c_str());
    dump_section("midi",     SRC_MIDI_CC,   false);
    dump_section("midi",     SRC_MIDI_CC14, false);
    dump_section("midi",     SRC_MIDI_NOTE, false);

    std::fclose(f);
    return true;
}
