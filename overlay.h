#pragma once
#include <GL/glew.h>
#include <string>
#include <vector>

// Non-feedback overlay: cumulative HUD bottom-left + toggle-on-H help screen.
// Drawn AFTER the recorder captures, so overlays never appear in the MP4.

class Overlay {
public:
    bool init();
    void shutdown();
    void resize(int w, int h);

    // Call once per frame, after blit + recorder.capture(), before SwapBuffers.
    void draw();

    // Log a parameter change. `key` groups related presses so repeated taps
    // on Q accumulate into one HUD line showing total delta this burst.
    // `label` is the displayed name, `deltaFormatted` is e.g. "+0.06 deg/pass",
    // `valueFormatted` is e.g. "34.4 deg/s".
    void logParam(const std::string& key,
                  const std::string& label,
                  const std::string& deltaFormatted,
                  const std::string& valueFormatted);

    // Non-accumulable event (layer toggle, inject, clear, etc).
    void logEvent(const std::string& text);

    // Help screen toggle.
    void toggleHelp() { helpVisible_ = !helpVisible_; }
    bool helpVisible() const { return helpVisible_; }

    // Update the text shown in the help overlay. Caller (main) should call
    // this every frame while help is visible so live values stay current.
    void setHelpText(const std::string& s) { helpText_ = s; }

private:
    struct Line {
        std::string key;      // "" for discrete events
        std::string text;     // the formatted string to render
        double      lastTouch = 0.0;
    };

    // A running delta accumulates per-key during a burst of activity. When
    // the whole HUD has been quiet long enough to fully fade, we clear both.
    std::vector<Line> lines_;
    double lastActivity_ = 0.0;
    int    winW_ = 0, winH_ = 0;
    bool   helpVisible_ = false;
    std::string helpText_;

    // GL resources for text rendering
    GLuint prog_ = 0, vbo_ = 0, vao_ = 0;
    GLint  locRes_ = -1, locAlpha_ = -1;

    void drawTextLine(float x, float y, const std::string& text,
                      unsigned char rgba[4], float alpha, float scale = 1.0f);
    void drawFilledRect(float x, float y, float w, float h,
                        unsigned char rgba[4], float alpha);
};
