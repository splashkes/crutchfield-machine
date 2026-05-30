#pragma once

#include <GL/glew.h>
#include <functional>
#include <string>
#include <vector>

enum class UiControlKind {
    Slider,
    Toggle,
    Integer,
};

struct UiControl {
    std::string id;
    std::string label;
    std::string section;
    UiControlKind kind = UiControlKind::Slider;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float step = 0.01f;
    std::function<float()> get;
    std::function<void(float)> set;
    std::function<std::string(float)> format;
    std::function<bool()> enabled;
};

class UiPanel {
public:
    bool init();
    void shutdown();
    void resize(int w, int h);
    void draw();

    bool loadLayout(const std::string& path);
    void setControls(std::vector<UiControl> controls);

    void toggle();
    void close();
    bool visible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }
    void setWindowedHandler(std::function<void()> handler) { windowedHandler_ = std::move(handler); }

    bool key(int key, int action, int mods);
    bool textInput(unsigned int codepoint);
    bool mouseButton(int button, int action, double x, double y);
    bool cursor(double x, double y);

    // Math augmentation — host calls each frame with the current
    // dynamical regime so the dock can show a colored badge next to
    // the pinned controls. Empty name disables.
    void setRegimeBadge(const std::string& name,
                        unsigned char r, unsigned char g, unsigned char b) {
        regimeName_ = name;
        regimeColor_[0] = r; regimeColor_[1] = g; regimeColor_[2] = b;
    }

private:
    struct Section {
        std::string id;
        std::string label;
        std::string visualizer;
        std::vector<std::string> controls;
    };

    enum class HitKind {
        Section,
        Control,
        Slider,
        ToggleButton,
        Pin,
        Reorder,
        Windowed,
    };

    struct Hit {
        HitKind kind;
        int index = -1;
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        float sliderX = 0.0f, sliderW = 0.0f;
    };

    int winW_ = 0, winH_ = 0;
    bool visible_ = false;
    bool dragging_ = false;
    bool reorderDragging_ = false;
    // Regime badge (math augmentation)
    std::string regimeName_;
    unsigned char regimeColor_[3] = { 130, 220, 150 };
    int activeSection_ = 0;
    int selectedControl_ = -1;
    int dragControl_ = -1;
    int reorderControl_ = -1;
    bool editing_ = false;
    std::string editBuffer_;

    std::string title_ = "Crutchfield Control";
    std::function<void()> windowedHandler_;
    std::vector<Section> sections_;
    std::vector<std::string> pinned_;
    std::vector<UiControl> controls_;
    std::vector<Hit> hits_;

    GLuint prog_ = 0, vbo_ = 0, vao_ = 0;
    GLint locRes_ = -1, locAlpha_ = -1;

    void installDefaultLayout();
    int controlIndexById(const std::string& id) const;
    int controlIndexInActiveSection(int visibleRow) const;
    std::vector<int> activeControlIndices() const;
    std::string valueText(int idx) const;
    void setControlValue(int idx, float value);
    void stepControl(int idx, float dir);
    bool controlEnabled(int idx) const;
    void togglePinned(int idx);
    bool isPinned(const std::string& id) const;
    bool activeSectionIsPinned() const;
    void movePinnedControl(int idx, int newPos);
    void beginEdit(int idx);
    void commitEdit();
    void cancelEdit();
    void selectNext(int dir);

    void drawFullPanel();
    void drawDock();
    void drawLayerVisualizer(float x, float y, float w, float h);
    void drawPreview(float x, float y, float w, float h);
    void drawContextPanel(float x, float y, float w, float h);
    void drawSliderRow(int idx, float x, float y, float w, bool pinnedRow);
    void drawToggleRow(int idx, float x, float y, float w, bool pinnedRow);
    void drawText(float x, float y, const std::string& text,
                  unsigned char rgba[4], float alpha, float scale = 1.0f);
    void drawRect(float x, float y, float w, float h,
                  unsigned char rgba[4], float alpha);
    void drawOutline(float x, float y, float w, float h,
                     unsigned char rgba[4], float alpha);
};
