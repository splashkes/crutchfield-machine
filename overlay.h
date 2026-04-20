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
    void drawFilledRect(float x, float y, float w, float h,
                        unsigned char rgba[4], float alpha);

    void drawHelpPanel();
    void drawHelpMenu(float x, float y, float w, float h);
    void drawHelpSection(float x, float y, float w, float h);

    // Helper: split body text into lines for scroll handling.
    static std::vector<std::string> splitLines(const std::string& s);
};
