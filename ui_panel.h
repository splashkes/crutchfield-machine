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

    bool key(int key, int action, int mods);
    bool textInput(unsigned int codepoint);
    bool mouseButton(int button, int action, double x, double y);
    bool cursor(double x, double y);

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
        Pin,
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
    int activeSection_ = 0;
    int selectedControl_ = -1;
    int dragControl_ = -1;
    bool editing_ = false;
    std::string editBuffer_;

    std::string title_ = "Crutchfield Control";
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
    void togglePinned(int idx);
    bool isPinned(const std::string& id) const;
    void beginEdit(int idx);
    void commitEdit();
    void cancelEdit();
    void selectNext(int dir);

    void drawFullPanel();
    void drawDock();
    void drawLayerVisualizer(float x, float y, float w, float h);
    void drawPreview(float x, float y, float w, float h);
    void drawSliderRow(int idx, float x, float y, float w, bool pinnedRow);
    void drawToggleRow(int idx, float x, float y, float w, bool pinnedRow);
    void drawText(float x, float y, const std::string& text,
                  unsigned char rgba[4], float alpha, float scale = 1.0f);
    void drawRect(float x, float y, float w, float h,
                  unsigned char rgba[4], float alpha);
    void drawOutline(float x, float y, float w, float h,
                     unsigned char rgba[4], float alpha);
};
