#pragma once
#include <GL/glew.h>
#include <functional>
#include <string>
#include <vector>

// Non-feedback overlay: cumulative HUD bottom-left + top-left drill-down
// help panel. Drawn AFTER the recorder captures, so overlays never
// appear in the MP4.

class Overlay {
public:
    bool init();
    void shutdown();
    void resize(int w, int h);

    // Call once per frame, after blit + recorder.capture(), before SwapBuffers.
    void draw();

    // Log a parameter change. `key` groups related presses so repeated taps
    // on Q accumulate into one HUD line showing total delta this burst.
    void logParam(const std::string& key,
                  const std::string& label,
                  const std::string& deltaFormatted,
                  const std::string& valueFormatted);

    // Non-accumulable event (layer toggle, inject, clear, etc).
    void logEvent(const std::string& text);

    // ── Help panel ────────────────────────────────────────────────────
    // Two levels: menu (list of sections) and section (one section's
    // content). Panel is fixed top-left and does NOT dim the main view,
    // so you can keep it open while driving the instrument.
    //
    // Host calls setHelpSections() once at startup with the ordered list
    // of section names, and setHelpProvider() with a callback that returns
    // the up-to-date body for a given section index. The overlay calls
    // the provider every frame while a section is viewed — values stay
    // live without the host having to push them.
    using BodyProvider   = std::function<std::string(int section)>;
    using LegendProvider = std::function<std::string(int section)>;
    void setHelpSections(std::vector<std::string> names);
    void setHelpProvider(BodyProvider p)   { provider_ = std::move(p); }
    void setLegendProvider(LegendProvider p){ legend_   = std::move(p); }

    // Index of the section the user has drilled into (or is hovering in
    // menu view). -1 if help closed and no section has ever been entered.
    int  activeSection() const { return activeSection_; }
    void setActiveSection(int idx) {
        activeSection_ = idx;
        if (idx >= 0 && idx < (int)sections_.size()) menuSel_ = idx;
    }
    bool inSectionView() const { return view_ == VIEW_SECTION; }

    // Small always-visible bottom-right tag telling the user which
    // section the controller is driving and how to open help. Host
    // toggles this based on whether a gamepad is connected.
    void setShowGamepadHint(bool b) { showGamepadHint_ = b; }

    // Navigation. Meant to be wired to UI actions — host dispatches these
    // when the help panel is visible.
    void toggleHelp();
    void helpUp();
    void helpDown();
    void helpEnter();
    void helpBack();     // section → menu; menu → close

    bool helpVisible() const { return helpVisible_; }

    // ── Math dashboard ────────────────────────────────────────────────
    // An elegant, semi-transparent panel that exposes the mathematical
    // characterization of the running feedback system: spectral radius,
    // memory half-life, diffusion coefficient, coupling strength, noise
    // floor — and predicts stability/chaos regime. Plus sparklines for
    // every continuous parameter over the last few seconds.
    //
    // The host calls mathPushFrame() once per frame with the current
    // Params snapshot; the overlay maintains its own ring buffer.
    struct MathSample {
        float decay, blurX, blurY, chroma, gamma, satGain, contrast,
              hueRate, noise, couple, external, sphereReverb, outFade;
        float zoom, theta;
    };
    void toggleMath() {
        mathVisible_ = !mathVisible_;
        if (mathVisible_ && mathSelectedRow_ < 0) mathSelectedRow_ = 0;
    }
    bool mathVisible() const { return mathVisible_; }
    void mathPushFrame(const MathSample& s);

    // Cursor navigation in the Mathlab panel. While mathVisible, the
    // host should intercept its arrow keys (translate normally) and
    // route them here. mathSelectedActionDec()/Inc() return the
    // ActionId to fire for "lower / raise the current parameter" —
    // host's apply_action will land them in the existing dispatch.
    void  mathSelectNext();
    void  mathSelectPrev();
    int   mathSelectedRow() const { return mathSelectedRow_; }
    int   mathNumRows() const;                       // total selectable rows
    int   mathSelectedActionDec() const;             // returns int ActionId
    int   mathSelectedActionInc() const;

    // Mouse drag editing. When the user clicks-and-drags inside the
    // Mathlab panel's slider area, the host calls these to translate
    // pixel coords into "fire setAxis for the active row at value V".
    // Returns true if the panel handled the event (so the host should
    // not also dispatch a view-rotate or other downstream interaction).
    bool  mathMouseDown(double mx, double my);
    bool  mathMouseDrag(double mx, double my);
    void  mathMouseUp();

    // Called each frame; pulls the host's apply_action via the supplied
    // dispatch callback so a drag fires the setAxis action with the
    // mapped value. The dispatcher signature matches Input::Handler.
    void  mathTickDrag(const std::function<void(int actionId, float value)>& dispatch);

private:
    enum View { VIEW_MENU, VIEW_SECTION };

    struct Line {
        std::string key;      // "" for discrete events
        std::string text;     // the formatted string to render
        double      lastTouch = 0.0;
    };

    std::vector<Line> lines_;
    double lastActivity_ = 0.0;
    int    winW_ = 0, winH_ = 0;

    // Help state
    bool   helpVisible_ = false;
    View   view_ = VIEW_MENU;
    int    menuSel_ = 0;
    int    sectionScroll_ = 0;
    std::vector<std::string> sections_;
    BodyProvider   provider_;
    LegendProvider legend_;
    std::string  cachedBody_;          // last snapshot from provider (debug)
    int          activeSection_ = -1;
    bool         showGamepadHint_ = false;

    // GL resources for text rendering
    GLuint prog_ = 0, vbo_ = 0, vao_ = 0;
    GLint  locRes_ = -1, locAlpha_ = -1;

    void drawTextLine(float x, float y, const std::string& text,
                      unsigned char rgba[4], float alpha, float scale = 1.0f);
    // Same as drawTextLine but lays down a narrow dark strip behind each
    // non-empty line so the text stays legible over bright feedback.
    // Used by the help panel — no more full-panel dimming.
    void drawTextBacked(float x, float y, const std::string& text,
                        unsigned char rgba[4], float scale = 1.0f);
    void drawFilledRect(float x, float y, float w, float h,
                        unsigned char rgba[4], float alpha);

    void drawHelpPanel();
    void drawHelpMenu(float x, float y, float w, float h);
    void drawHelpSection(float x, float y, float w, float h);

    // Math dashboard
    bool mathVisible_ = false;
    int  mathSelectedRow_ = -1;            // -1 until first open
    // Hit list for the interactive Dynamics panel. drawMathPanel
    // rebuilds it every frame; mouse handlers route through it.
  public:
    enum MathHitKind {
        MHIT_NONE = 0,
        MHIT_SLIDER_DIST,      // regime.distance.axis    (horizontal slider)
        MHIT_SLIDER_HALFLIFE,  // dyn.halflife.axis       (horizontal slider)
        MHIT_PAD_COMPASS,      // pad.regime.x + .y       (2D pad)
        MHIT_BUTTON_REGIME,    // regime.set              (value = idx)
        MHIT_BUTTON_INVERT,    // regime.invert
        MHIT_BUTTON_FAILSAFE,  // theater.failsafe
        MHIT_BUTTON_ECHO,      // math.echo
        MHIT_BUTTON_SNAP_SAVE, // snapshot.save           (value = slot)
        MHIT_BUTTON_SNAP_RECALL_STABLE, // recall most-recent STABLE snap
    };
    struct MathHit {
        MathHitKind kind;
        float x, y, w, h;
        int   value;           // slot number, regime index, etc.
    };
  private:
  public:
    struct PendingDispatch { int actionId; float value; };
  private:
    std::vector<MathHit> mathHits_;
    // Mouse drag state (single active hit at a time).
    int    mathActiveHit_  = -1;
    bool   mathDragArmed_  = false;
    // Pending actions for the host's apply_action; drained in
    // mathTickDrag.
    std::vector<PendingDispatch> mathPending_;
    bool   mathDragging_ = false;
    int    mathDragRow_  = -1;
    double mathDragX_    = 0.0;
    bool   mathDragPending_ = false;       // legacy; no longer set
    float  mathDragValue_   = 0.f;
    std::vector<MathSample> mathRing_;     // ring buffer; ~240 samples = 4s @ 60fps
    int                     mathRingHead_ = 0;
    int                     mathRingCount_ = 0;
    static constexpr int    MATH_RING_CAP = 360;
    void drawMathPanel();
    // Render a small sparkline of one scalar from the math ring.
    // x,y,w,h are pixel rects; value_accessor returns one float per
    // MathSample. min/max define the y-axis range; pass min=max=NaN
    // (the function will use auto-scale).
    void drawSparkline(float x, float y, float w, float h,
                       float (*accessor)(const MathSample&),
                       float vmin, float vmax,
                       unsigned char rgba[4]);

    // Helper: split body text into lines for scroll handling.
    static std::vector<std::string> splitLines(const std::string& s);
};
