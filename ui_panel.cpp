#include "ui_panel.h"
#include "stb_easy_font.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

static const char* UI_VS = R"(#version 410 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aCol;
uniform vec2 uRes;
out vec4 vCol;
void main() {
    vec2 ndc = vec2(aPos.x / uRes.x * 2.0 - 1.0,
                    1.0 - aPos.y / uRes.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vCol = aCol;
}
)";

static const char* UI_FS = R"(#version 410 core
in vec4 vCol;
out vec4 oCol;
uniform float uAlpha;
void main() { oCol = vec4(vCol.rgb, vCol.a * uAlpha); }
)";

static GLuint compileUiShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        fprintf(stderr, "[ui] shader compile: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static std::string trim(std::string s) {
    auto isws = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && isws((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && isws((unsigned char)s.back())) s.pop_back();
    return s;
}

static bool startsWith(const std::string& s, const char* p) {
    return s.rfind(p, 0) == 0;
}

static std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                          (s.front() == '\'' && s.back() == '\''))) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

static std::vector<std::string> parseList(const std::string& line) {
    size_t a = line.find('[');
    size_t b = line.find(']', a == std::string::npos ? 0 : a + 1);
    if (a == std::string::npos || b == std::string::npos || b <= a) return {};
    std::string body = line.substr(a + 1, b - a - 1);
    std::vector<std::string> out;
    std::stringstream ss(body);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = unquote(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

static float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

bool UiPanel::init() {
    GLuint vs = compileUiShader(GL_VERTEX_SHADER, UI_VS);
    GLuint fs = compileUiShader(GL_FRAGMENT_SHADER, UI_FS);
    if (!vs || !fs) return false;
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs);
    glAttachShader(prog_, fs);
    glLinkProgram(prog_);
    GLint ok = 0;
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        fprintf(stderr, "[ui] shader link failed\n");
        glDeleteProgram(prog_);
        prog_ = 0;
        return false;
    }

    locRes_ = glGetUniformLocation(prog_, "uRes");
    locAlpha_ = glGetUniformLocation(prog_, "uAlpha");

    glGenBuffers(1, &vbo_);
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 16, (void*)12);
    return true;
}

void UiPanel::shutdown() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (prog_) glDeleteProgram(prog_);
    vbo_ = vao_ = prog_ = 0;
}

void UiPanel::resize(int w, int h) {
    winW_ = w;
    winH_ = h;
}

void UiPanel::installDefaultLayout() {
    title_ = "Crutchfield Control";
    pinned_ = {"decay", "external", "sphereMode", "sphereReverb", "outFade"};
    sections_.clear();
    sections_.push_back({"pinned", "Pinned", "", {}});
    sections_.push_back({"layers", "Layers", "layers", {
        "layer.warp", "layer.optics", "layer.gamma", "layer.color",
        "layer.contrast", "layer.decay", "layer.noise", "layer.couple",
        "layer.external", "layer.inject", "layer.physics", "layer.thermal"
    }});
    sections_.push_back({"tone", "Tone", "", {
        "decay", "contrast", "gamma", "satGain", "hueRate", "outFade",
        "brightness"
    }});
    sections_.push_back({"feedback", "Feedback", "", {
        "zoom", "theta", "external", "sourceWet", "fxWet", "borderSize",
        "borderSoftness", "borderDecay", "couple", "noise"
    }});
    sections_.push_back({"sphere", "Sphere", "", {
        "sphereMode", "sphereReverb", "viewRotX", "viewRotY"
    }});
    sections_.push_back({"inject", "Inject", "", {
        "pattern", "shapeKind", "shapeCount", "shapeSize", "shapeAngle"
    }});
    activeSection_ = std::min(activeSection_, (int)sections_.size() - 1);
}

bool UiPanel::loadLayout(const std::string& path) {
    installDefaultLayout();

    std::ifstream f(path);
    if (!f) {
        fprintf(stderr, "[ui] using built-in layout; can't open %s\n", path.c_str());
        return false;
    }

    std::vector<Section> parsedSections;
    std::vector<std::string> parsedPins;
    Section* cur = nullptr;
    std::string line;
    while (std::getline(f, line)) {
        size_t comment = line.find('#');
        if (comment != std::string::npos) line = line.substr(0, comment);
        line = trim(line);
        if (line.empty()) continue;

        if (startsWith(line, "title:")) {
            title_ = unquote(line.substr(6));
        } else if (startsWith(line, "pinned:")) {
            parsedPins = parseList(line);
        } else if (startsWith(line, "- id:")) {
            parsedSections.push_back({});
            cur = &parsedSections.back();
            cur->id = unquote(line.substr(5));
            cur->label = cur->id;
        } else if (cur && startsWith(line, "label:")) {
            cur->label = unquote(line.substr(6));
        } else if (cur && startsWith(line, "visualizer:")) {
            cur->visualizer = unquote(line.substr(11));
        } else if (cur && startsWith(line, "controls:")) {
            cur->controls = parseList(line);
        } else if (cur && startsWith(line, "pins:")) {
            auto pins = parseList(line);
            parsedPins.insert(parsedPins.end(), pins.begin(), pins.end());
        }
    }

    if (!parsedPins.empty()) pinned_ = parsedPins;
    if (!parsedSections.empty()) sections_ = parsedSections;
    activeSection_ = std::min(activeSection_, (int)sections_.size() - 1);
    fprintf(stderr, "[ui] loaded %s\n", path.c_str());
    return true;
}

void UiPanel::setControls(std::vector<UiControl> controls) {
    controls_ = std::move(controls);
}

void UiPanel::toggle() {
    visible_ = !visible_;
    if (!visible_) cancelEdit();
}

void UiPanel::close() {
    visible_ = false;
    cancelEdit();
}

int UiPanel::controlIndexById(const std::string& id) const {
    for (int i = 0; i < (int)controls_.size(); i++)
        if (controls_[i].id == id) return i;
    return -1;
}

std::vector<int> UiPanel::activeControlIndices() const {
    std::vector<int> out;
    if (sections_.empty()) return out;
    const Section& sec = sections_[std::max(0, std::min(activeSection_, (int)sections_.size() - 1))];
    if (sec.id == "pinned") {
        for (const auto& id : pinned_) {
            int idx = controlIndexById(id);
            if (idx >= 0) out.push_back(idx);
        }
        return out;
    }
    for (const auto& id : sec.controls) {
        int idx = controlIndexById(id);
        if (idx >= 0) out.push_back(idx);
    }
    return out;
}

int UiPanel::controlIndexInActiveSection(int visibleRow) const {
    auto rows = activeControlIndices();
    if (visibleRow < 0 || visibleRow >= (int)rows.size()) return -1;
    return rows[visibleRow];
}

std::string UiPanel::valueText(int idx) const {
    if (idx < 0 || idx >= (int)controls_.size() || !controls_[idx].get) return "";
    float v = controls_[idx].get();
    if (editing_ && idx == selectedControl_) return editBuffer_ + "_";
    if (controls_[idx].format) return controls_[idx].format(v);
    char b[48];
    if (controls_[idx].kind == UiControlKind::Toggle) {
        snprintf(b, sizeof b, "%s", v >= 0.5f ? "ON" : "off");
    } else if (controls_[idx].kind == UiControlKind::Integer) {
        snprintf(b, sizeof b, "%d", (int)lroundf(v));
    } else {
        snprintf(b, sizeof b, "%.3f", v);
    }
    return b;
}

bool UiPanel::controlEnabled(int idx) const {
    if (idx < 0 || idx >= (int)controls_.size()) return false;
    return controls_[idx].enabled ? controls_[idx].enabled() : true;
}

void UiPanel::setControlValue(int idx, float value) {
    if (idx < 0 || idx >= (int)controls_.size() || !controls_[idx].set) return;
    const UiControl& c = controls_[idx];
    float v = clampf(value, c.minValue, c.maxValue);
    if (c.kind == UiControlKind::Integer) v = roundf(v);
    if (c.kind == UiControlKind::Toggle) v = v >= 0.5f ? 1.0f : 0.0f;
    c.set(v);
}

void UiPanel::stepControl(int idx, float dir) {
    if (idx < 0 || idx >= (int)controls_.size() || !controls_[idx].get) return;
    const UiControl& c = controls_[idx];
    float step = c.step > 0.0f ? c.step : (c.maxValue - c.minValue) * 0.01f;
    if (c.kind == UiControlKind::Toggle) {
        setControlValue(idx, c.get() >= 0.5f ? 0.0f : 1.0f);
    } else {
        setControlValue(idx, c.get() + dir * step);
    }
}

bool UiPanel::isPinned(const std::string& id) const {
    return std::find(pinned_.begin(), pinned_.end(), id) != pinned_.end();
}

bool UiPanel::activeSectionIsPinned() const {
    if (sections_.empty()) return false;
    int idx = std::max(0, std::min(activeSection_, (int)sections_.size() - 1));
    return sections_[idx].id == "pinned";
}

void UiPanel::movePinnedControl(int idx, int newPos) {
    if (idx < 0 || idx >= (int)controls_.size()) return;
    const std::string id = controls_[idx].id;
    auto it = std::find(pinned_.begin(), pinned_.end(), id);
    if (it == pinned_.end()) return;
    std::string moved = *it;
    pinned_.erase(it);
    newPos = std::max(0, std::min(newPos, (int)pinned_.size()));
    pinned_.insert(pinned_.begin() + newPos, moved);
}

void UiPanel::togglePinned(int idx) {
    if (idx < 0 || idx >= (int)controls_.size()) return;
    const std::string& id = controls_[idx].id;
    auto it = std::find(pinned_.begin(), pinned_.end(), id);
    if (it == pinned_.end()) pinned_.push_back(id);
    else pinned_.erase(it);
}

void UiPanel::beginEdit(int idx) {
    if (idx < 0 || idx >= (int)controls_.size()) return;
    if (controls_[idx].kind == UiControlKind::Toggle) return;
    selectedControl_ = idx;
    editing_ = true;
    editBuffer_.clear();
}

void UiPanel::commitEdit() {
    if (!editing_) return;
    char* end = nullptr;
    float v = strtof(editBuffer_.c_str(), &end);
    if (end && end != editBuffer_.c_str()) setControlValue(selectedControl_, v);
    cancelEdit();
}

void UiPanel::cancelEdit() {
    editing_ = false;
    editBuffer_.clear();
}

void UiPanel::selectNext(int dir) {
    auto rows = activeControlIndices();
    if (rows.empty()) return;
    int pos = 0;
    for (int i = 0; i < (int)rows.size(); i++) {
        if (rows[i] == selectedControl_) {
            pos = i;
            break;
        }
    }
    pos = (pos + dir + (int)rows.size()) % (int)rows.size();
    selectedControl_ = rows[pos];
}

bool UiPanel::key(int key, int action, int mods) {
    if (!visible_) return false;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return editing_;
    (void)mods;

    if (editing_) {
        if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) commitEdit();
        else if (key == GLFW_KEY_ESCAPE) cancelEdit();
        else if (key == GLFW_KEY_BACKSPACE && !editBuffer_.empty()) editBuffer_.pop_back();
        return true;
    }

    if (key == GLFW_KEY_ESCAPE) { close(); return true; }
    if (key == GLFW_KEY_UP) { selectNext(-1); return true; }
    if (key == GLFW_KEY_DOWN) { selectNext(1); return true; }
    if (key == GLFW_KEY_LEFT) { stepControl(selectedControl_, -1.0f); return true; }
    if (key == GLFW_KEY_RIGHT) { stepControl(selectedControl_, 1.0f); return true; }
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        if (selectedControl_ >= 0 &&
            controls_[selectedControl_].kind == UiControlKind::Toggle) {
            stepControl(selectedControl_, 1.0f);
        } else {
            beginEdit(selectedControl_);
        }
        return true;
    }
    if ((key == GLFW_KEY_RIGHT_BRACKET || key == GLFW_KEY_PAGE_DOWN) && !sections_.empty()) {
        activeSection_ = (activeSection_ + 1) % (int)sections_.size();
        auto rows = activeControlIndices();
        selectedControl_ = rows.empty() ? -1 : rows.front();
        return true;
    }
    if ((key == GLFW_KEY_LEFT_BRACKET || key == GLFW_KEY_PAGE_UP) && !sections_.empty()) {
        activeSection_ = (activeSection_ - 1 + (int)sections_.size()) % (int)sections_.size();
        auto rows = activeControlIndices();
        selectedControl_ = rows.empty() ? -1 : rows.front();
        return true;
    }
    if (key == GLFW_KEY_P) { togglePinned(selectedControl_); return true; }
    return false;
}

bool UiPanel::textInput(unsigned int codepoint) {
    if (!visible_ || !editing_) return false;
    if (codepoint < 128) {
        char c = (char)codepoint;
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
            editBuffer_.push_back(c);
        }
    }
    return true;
}

bool UiPanel::mouseButton(int button, int action, double x, double y) {
    if (button != GLFW_MOUSE_BUTTON_LEFT || !visible_) return false;
    if (action == GLFW_RELEASE) {
        dragging_ = false;
        reorderDragging_ = false;
        dragControl_ = -1;
        reorderControl_ = -1;
        return true;
    }
    if (action != GLFW_PRESS) return true;

    cancelEdit();
    for (const Hit& h : hits_) {
        if (x < h.x || x > h.x + h.w || y < h.y || y > h.y + h.h) continue;
        if (h.kind == HitKind::Windowed) {
            if (windowedHandler_) windowedHandler_();
            return true;
        }
        if (h.kind == HitKind::Pin) {
            togglePinned(h.index);
            selectedControl_ = h.index;
            return true;
        }
        if (h.kind == HitKind::Reorder) {
            selectedControl_ = h.index;
            reorderDragging_ = true;
            reorderControl_ = h.index;
            return true;
        }
    }
    for (const Hit& h : hits_) {
        if (x < h.x || x > h.x + h.w || y < h.y || y > h.y + h.h) continue;
        if (h.kind == HitKind::ToggleButton) {
            selectedControl_ = h.index;
            stepControl(h.index, 1.0f);
            return true;
        }
        if (h.kind == HitKind::Slider) {
            selectedControl_ = h.index;
            const UiControl& c = controls_[h.index];
            float t = (float)((x - h.sliderX) / h.sliderW);
            setControlValue(h.index, c.minValue + t * (c.maxValue - c.minValue));
            dragging_ = true;
            dragControl_ = h.index;
            return true;
        }
    }
    for (const Hit& h : hits_) {
        if (x < h.x || x > h.x + h.w || y < h.y || y > h.y + h.h) continue;
        if (h.kind == HitKind::Section) {
            activeSection_ = h.index;
            auto rows = activeControlIndices();
            selectedControl_ = rows.empty() ? -1 : rows.front();
            return true;
        }
        if (h.kind == HitKind::Control) {
            selectedControl_ = h.index;
            return true;
        }
    }
    return true;
}

bool UiPanel::cursor(double x, double y) {
    if (!visible_) return false;
    if (reorderDragging_ && reorderControl_ >= 0 && activeSectionIsPinned()) {
        int pos = 0;
        int seen = 0;
        for (const Hit& h : hits_) {
            if (h.kind != HitKind::Control) continue;
            if (y > h.y + h.h * 0.5f) pos = seen + 1;
            seen++;
        }
        movePinnedControl(reorderControl_, pos);
        return true;
    }
    if (!dragging_ || dragControl_ < 0) return true;
    for (const Hit& h : hits_) {
        if (h.kind != HitKind::Slider || h.index != dragControl_) continue;
        const UiControl& c = controls_[h.index];
        float t = (float)((x - h.sliderX) / h.sliderW);
        setControlValue(h.index, c.minValue + t * (c.maxValue - c.minValue));
        break;
    }
    (void)y;
    return true;
}

void UiPanel::drawRect(float x, float y, float w, float h,
                       unsigned char rgba[4], float alpha) {
    struct V { float x, y, z; unsigned char c[4]; };
    V v[6] = {
        {x, y, 0.0f, {rgba[0], rgba[1], rgba[2], rgba[3]}},
        {x + w, y, 0.0f, {rgba[0], rgba[1], rgba[2], rgba[3]}},
        {x + w, y + h, 0.0f, {rgba[0], rgba[1], rgba[2], rgba[3]}},
        {x, y, 0.0f, {rgba[0], rgba[1], rgba[2], rgba[3]}},
        {x + w, y + h, 0.0f, {rgba[0], rgba[1], rgba[2], rgba[3]}},
        {x, y + h, 0.0f, {rgba[0], rgba[1], rgba[2], rgba[3]}},
    };
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
    glUseProgram(prog_);
    glUniform2f(locRes_, (float)winW_, (float)winH_);
    glUniform1f(locAlpha_, alpha);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void UiPanel::drawOutline(float x, float y, float w, float h,
                          unsigned char rgba[4], float alpha) {
    drawRect(x, y, w, 1.0f, rgba, alpha);
    drawRect(x, y + h - 1.0f, w, 1.0f, rgba, alpha);
    drawRect(x, y, 1.0f, h, rgba, alpha);
    drawRect(x + w - 1.0f, y, 1.0f, h, rgba, alpha);
}

void UiPanel::drawText(float x, float y, const std::string& text,
                       unsigned char rgba[4], float alpha, float scale) {
    const int VBUF_BYTES = 256 * 270 + 1024;
    static std::vector<char> vbuf(VBUF_BYTES);
    static std::vector<float> tris;
    int nq = stb_easy_font_print(x, y, (char*)text.c_str(), rgba, vbuf.data(), (int)vbuf.size());
    if (nq <= 0) return;
    if (scale != 1.0f) {
        float* fv = (float*)vbuf.data();
        for (int k = 0; k < nq * 4; k++) {
            fv[k * 4 + 0] = x + (fv[k * 4 + 0] - x) * scale;
            fv[k * 4 + 1] = y + (fv[k * 4 + 1] - y) * scale;
        }
    }
    tris.clear();
    const float* qv = (const float*)vbuf.data();
    for (int q = 0; q < nq; q++) {
        const float* q0 = qv + (q * 4 + 0) * 4;
        const float* q1 = qv + (q * 4 + 1) * 4;
        const float* q2 = qv + (q * 4 + 2) * 4;
        const float* q3 = qv + (q * 4 + 3) * 4;
        for (int v = 0; v < 4; v++) tris.push_back(q0[v]);
        for (int v = 0; v < 4; v++) tris.push_back(q1[v]);
        for (int v = 0; v < 4; v++) tris.push_back(q2[v]);
        for (int v = 0; v < 4; v++) tris.push_back(q0[v]);
        for (int v = 0; v < 4; v++) tris.push_back(q2[v]);
        for (int v = 0; v < 4; v++) tris.push_back(q3[v]);
    }
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, tris.size() * sizeof(float), tris.data(), GL_STREAM_DRAW);
    glUseProgram(prog_);
    glUniform2f(locRes_, (float)winW_, (float)winH_);
    glUniform1f(locAlpha_, alpha);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(tris.size() / 4));
}

void UiPanel::drawSliderRow(int idx, float x, float y, float w, bool pinnedRow) {
    if (idx < 0 || idx >= (int)controls_.size()) return;
    const UiControl& c = controls_[idx];
    const bool enabled = controlEnabled(idx);
    const bool sel = idx == selectedControl_;
    unsigned char bg[4] = {18, 22, 24, 255};
    unsigned char hi[4] = {92, 214, 170, 255};
    unsigned char dim[4] = {125, 136, 134, 255};
    unsigned char fg[4] = {232, 238, 235, 255};
    unsigned char line[4] = {48, 58, 58, 255};
    drawRect(x, y, w, 40.0f, bg, sel ? 0.74f : 0.52f);
    if (!enabled) drawRect(x, y, w, 40.0f, dim, 0.18f);
    if (sel) drawOutline(x, y, w, 40.0f, enabled ? hi : dim, 0.85f);
    drawText(x + 12.0f, y + 12.0f, c.label, enabled ? fg : dim, enabled ? 0.96f : 0.62f, 1.65f);

    float valueX = x + w - 126.0f;
    drawText(valueX, y + 12.0f, enabled ? valueText(idx) : (valueText(idx) + " off"),
             enabled ? fg : dim, enabled ? 0.9f : 0.62f, 1.45f);

    float sx = x + (pinnedRow ? 162.0f : 220.0f);
    float sw = std::max(48.0f, valueX - sx - 14.0f);
    float v = c.get ? c.get() : 0.0f;
    float t = (c.maxValue > c.minValue) ? (v - c.minValue) / (c.maxValue - c.minValue) : 0.0f;
    t = clampf(t, 0.0f, 1.0f);
    drawRect(sx, y + 18.0f, sw, 7.0f, line, enabled ? 0.85f : 0.35f);
    drawRect(sx, y + 18.0f, sw * t, 7.0f, enabled ? hi : dim, enabled ? 0.95f : 0.38f);
    drawRect(sx + sw * t - 3.0f, y + 12.0f, 6.0f, 19.0f, enabled ? fg : dim, enabled ? 0.9f : 0.42f);
    hits_.push_back({HitKind::Control, idx, x, y, w - 34.0f, 40.0f, sx, sw});
    hits_.push_back({HitKind::Slider, idx, sx - 8.0f, y, sw + 16.0f, 40.0f, sx, sw});
    hits_.push_back({HitKind::Pin, idx, x + w - 30.0f, y + 8.0f, 24.0f, 24.0f, 0.0f, 0.0f});
    if (activeSectionIsPinned()) {
        hits_.push_back({HitKind::Reorder, idx, x + 4.0f, y + 6.0f, 22.0f, 28.0f, 0.0f, 0.0f});
        drawText(x + 7.0f, y + 12.0f, "::", dim, 0.85f, 1.25f);
    }
    drawText(x + w - 26.0f, y + 12.0f, isPinned(c.id) ? "*" : "+", isPinned(c.id) ? hi : dim, 0.95f, 1.45f);
}

void UiPanel::drawToggleRow(int idx, float x, float y, float w, bool pinnedRow) {
    (void)pinnedRow;
    if (idx < 0 || idx >= (int)controls_.size()) return;
    const UiControl& c = controls_[idx];
    const bool enabled = controlEnabled(idx);
    bool on = c.get && c.get() >= 0.5f;
    const bool sel = idx == selectedControl_;
    unsigned char bg[4] = {18, 22, 24, 255};
    unsigned char hi[4] = {92, 214, 170, 255};
    unsigned char off[4] = {72, 82, 84, 255};
    unsigned char fg[4] = {232, 238, 235, 255};
    drawRect(x, y, w, 40.0f, bg, sel ? 0.74f : 0.52f);
    if (!enabled) drawRect(x, y, w, 40.0f, off, 0.18f);
    if (sel) drawOutline(x, y, w, 40.0f, enabled ? hi : off, 0.85f);
    drawText(x + 12.0f, y + 12.0f, c.label, enabled ? fg : off, enabled ? 0.96f : 0.62f, 1.65f);
    drawRect(x + w - 98.0f, y + 10.0f, 56.0f, 20.0f, on ? hi : off, (on && enabled) ? 0.95f : 0.45f);
    drawText(x + w - 88.0f, y + 14.0f, on ? "ON" : "off", enabled ? fg : off, enabled ? 0.95f : 0.62f, 1.15f);
    hits_.push_back({HitKind::Control, idx, x, y, w - 34.0f, 40.0f, 0.0f, 0.0f});
    hits_.push_back({HitKind::ToggleButton, idx, x + w - 104.0f, y + 4.0f, 68.0f, 32.0f, 0.0f, 0.0f});
    hits_.push_back({HitKind::Pin, idx, x + w - 30.0f, y + 8.0f, 24.0f, 24.0f, 0.0f, 0.0f});
    if (activeSectionIsPinned()) {
        hits_.push_back({HitKind::Reorder, idx, x + 4.0f, y + 6.0f, 22.0f, 28.0f, 0.0f, 0.0f});
        drawText(x + 7.0f, y + 12.0f, "::", off, 0.85f, 1.25f);
    }
    drawText(x + w - 26.0f, y + 12.0f, isPinned(c.id) ? "*" : "+", on ? hi : off, 0.95f, 1.45f);
}

void UiPanel::drawLayerVisualizer(float x, float y, float w, float h) {
    unsigned char fg[4] = {228, 238, 234, 255};
    unsigned char dim[4] = {87, 96, 98, 255};
    unsigned char onCol[4] = {92, 214, 170, 255};
    unsigned char warn[4] = {220, 196, 68, 255};
    unsigned char bg[4] = {11, 13, 14, 255};
    unsigned char panel[4] = {18, 22, 24, 255};
    drawRect(x, y, w, h, bg, 0.34f);

    drawText(x + 10.0f, y + 10.0f, "layer control surface", fg, 0.92f, 1.5f);
    drawText(x + w - 166.0f, y + 12.0f, "click rows to toggle", dim, 0.82f, 1.0f);

    int onCount = 0;
    auto rows = activeControlIndices();
    for (int idx : rows) if (controls_[idx].get && controls_[idx].get() >= 0.5f) onCount++;

    char stats[80];
    snprintf(stats, sizeof stats, "%d / %d active", onCount, (int)rows.size());
    drawText(x + 10.0f, y + 36.0f, stats, fg, 0.86f, 1.25f);

    auto chip = [&](const char* id, const char* label, float cx, float cy, float cw) {
        int idx = controlIndexById(id);
        if (idx < 0) return;
        bool hot = controls_[idx].kind == UiControlKind::Toggle
            ? (controls_[idx].get && controls_[idx].get() >= 0.5f)
            : true;
        drawRect(cx, cy, cw, 28.0f, hot ? onCol : dim, hot ? 0.20f : 0.14f);
        drawText(cx + 8.0f, cy + 9.0f, std::string(label) + " " + valueText(idx),
                 hot ? fg : dim, hot ? 0.92f : 0.72f, 1.05f);
    };
    float chipY = y + 60.0f;
    float chipW = std::max(78.0f, (w - 30.0f) / 3.0f);
    chip("external", "external", x + 10.0f, chipY, chipW);
    chip("decay", "decay", x + 16.0f + chipW, chipY, chipW);
    chip("sphereMode", "sphere", x + 22.0f + chipW * 2.0f, chipY, chipW);

    float ry0 = y + 102.0f;
    float rowH = 30.0f;
    float gap = 5.0f;
    float usableRows = floorf((h - 112.0f) / (rowH + gap));
    int maxRows = std::max(0, std::min((int)rows.size(), (int)usableRows));
    for (int i = 0; i < (int)rows.size(); i++) {
        if (i >= maxRows) break;
        int idx = rows[i];
        float cy = ry0 + i * (rowH + gap);
        bool on = controls_[idx].get && controls_[idx].get() >= 0.5f;
        unsigned char* stateCol = on ? onCol : dim;

        drawRect(x + 10.0f, cy, w - 20.0f, rowH, panel, on ? 0.66f : 0.46f);
        drawRect(x + 10.0f, cy, 5.0f, rowH, stateCol, on ? 0.95f : 0.45f);
        if (idx == selectedControl_) drawOutline(x + 10.0f, cy, w - 20.0f, rowH, onCol, 0.85f);

        char nbuf[12];
        snprintf(nbuf, sizeof nbuf, "%02d", i + 1);
        drawText(x + 22.0f, cy + 9.0f, nbuf, dim, 0.9f, 1.05f);
        drawText(x + 55.0f, cy + 9.0f, controls_[idx].label, on ? fg : dim, on ? 0.96f : 0.72f, 1.22f);

        float pillX = x + w - 96.0f;
        drawRect(pillX, cy + 6.0f, 48.0f, 18.0f, stateCol, on ? 0.86f : 0.50f);
        drawText(pillX + 9.0f, cy + 10.0f, on ? "ON" : "off", fg, on ? 0.95f : 0.72f, 0.95f);
        hits_.push_back({HitKind::ToggleButton, idx, pillX - 6.0f, cy + 1.0f, 60.0f, rowH - 2.0f, 0.0f, 0.0f});

        float activity = on ? (0.38f + 0.62f * ((i % 5) / 4.0f)) : 0.08f;
        float bx = x + w - 42.0f;
        drawRect(bx, cy + 6.0f, 24.0f, 18.0f, dim, 0.20f);
        drawRect(bx, cy + 6.0f + 18.0f * (1.0f - activity), 24.0f, 18.0f * activity,
                 on ? warn : dim, on ? 0.62f : 0.26f);

        hits_.push_back({HitKind::Control, idx, x + 10.0f, cy, w - 20.0f, rowH, 0.0f, 0.0f});
    }

    if ((int)rows.size() > maxRows) {
        char more[64];
        snprintf(more, sizeof more, "+%d more layers in left list", (int)rows.size() - maxRows);
        drawText(x + 10.0f, y + h - 20.0f, more, dim, 0.86f, 1.0f);
    }
}

void UiPanel::drawPreview(float x, float y, float w, float h) {
    unsigned char fg[4] = {228, 238, 234, 255};
    unsigned char bg[4] = {11, 13, 14, 255};
    unsigned char a[4] = {88, 150, 210, 255};
    unsigned char b[4] = {220, 196, 68, 255};
    unsigned char c[4] = {92, 214, 170, 255};
    drawRect(x, y, w, h, bg, 0.34f);
    drawText(x + 10.0f, y + 10.0f, "response preview", fg, 0.88f, 1.45f);
    float px = x + 12.0f, py = y + 38.0f;
    float pw = w - 24.0f, ph = h - 52.0f;
    drawRect(px, py, pw, ph, a, 0.18f);
    for (int i = 0; i < 10; i++) {
        float t = i / 9.0f;
        unsigned char* col = (i % 2 == 0) ? b : c;
        drawRect(px + t * pw, py + (1.0f - t) * ph * 0.25f, pw / 12.0f, ph * (0.5f + 0.4f * t), col, 0.16f + 0.05f * i);
    }
    drawRect(px + pw * 0.57f, py + ph * 0.16f, pw * 0.28f, ph * 0.68f, c, 0.2f);
    drawOutline(px, py, pw, ph, fg, 0.18f);
}

void UiPanel::drawContextPanel(float x, float y, float w, float h) {
    unsigned char fg[4] = {228, 238, 234, 255};
    unsigned char dim[4] = {108, 120, 122, 255};
    unsigned char bg[4] = {11, 13, 14, 255};
    unsigned char panel[4] = {18, 22, 24, 255};
    unsigned char hi[4] = {92, 214, 170, 255};
    unsigned char warn[4] = {220, 196, 68, 255};

    drawRect(x, y, w, h, bg, 0.36f);
    drawText(x + 12.0f, y + 12.0f, "context", fg, 0.92f, 1.5f);

    int idx = selectedControl_;
    if (idx < 0 || idx >= (int)controls_.size()) {
        drawText(x + 12.0f, y + 48.0f, "select a middle-column control", dim, 0.86f, 1.15f);
        return;
    }

    const UiControl& c = controls_[idx];
    const bool pinned = isPinned(c.id);
    const bool enabled = controlEnabled(idx);
    const float v = c.get ? c.get() : 0.0f;

    drawText(x + 12.0f, y + 48.0f, c.label, enabled ? fg : dim, enabled ? 0.96f : 0.64f, 1.8f);
    drawText(x + 12.0f, y + 78.0f, c.id, dim, 0.78f, 1.05f);
    if (!enabled) {
        drawRect(x + w - 118.0f, y + 48.0f, 104.0f, 24.0f, warn, 0.22f);
        drawText(x + w - 106.0f, y + 55.0f, "layer off", warn, 0.95f, 1.0f);
    }

    drawRect(x + 12.0f, y + 106.0f, w - 24.0f, 52.0f, panel, 0.64f);
    drawText(x + 24.0f, y + 122.0f, "value", dim, 0.88f, 1.05f);
    drawText(x + 92.0f, y + 118.0f, valueText(idx), fg, 0.94f, 1.75f);

    if (c.kind != UiControlKind::Toggle) {
        float t = (c.maxValue > c.minValue) ? (v - c.minValue) / (c.maxValue - c.minValue) : 0.0f;
        t = clampf(t, 0.0f, 1.0f);
        drawRect(x + 12.0f, y + 176.0f, w - 24.0f, 8.0f, dim, 0.35f);
        drawRect(x + 12.0f, y + 176.0f, (w - 24.0f) * t, 8.0f, hi, 0.82f);
        char range[96];
        snprintf(range, sizeof range, "range %.3f .. %.3f   step %.4f", c.minValue, c.maxValue, c.step);
        drawText(x + 12.0f, y + 196.0f, range, dim, 0.82f, 1.0f);
    } else {
        drawText(x + 12.0f, y + 178.0f, "toggle control", dim, 0.82f, 1.0f);
    }

    float by = y + 232.0f;
    drawRect(x + 12.0f, by, w - 24.0f, 38.0f, pinned ? warn : hi, pinned ? 0.26f : 0.22f);
    drawOutline(x + 12.0f, by, w - 24.0f, 38.0f, pinned ? warn : hi, 0.72f);
    drawText(x + 24.0f, by + 13.0f, pinned ? "unpin from Pinned menu" : "pin to Pinned menu",
             fg, 0.95f, 1.25f);
    hits_.push_back({HitKind::Pin, idx, x + 12.0f, by, w - 24.0f, 38.0f, 0.0f, 0.0f});

    drawText(x + 12.0f, by + 64.0f, "keyboard", dim, 0.82f, 1.0f);
    drawText(x + 12.0f, by + 84.0f, "Enter exact value   <-/-> nudge   P pin", fg, 0.82f, 1.0f);
    drawText(x + 12.0f, by + 104.0f, "Space and performance keys pass through", fg, 0.82f, 1.0f);
}

void UiPanel::drawDock() {
    if (winW_ <= 0 || winH_ <= 0) return;
    unsigned char bg[4] = {8, 10, 11, 255};
    unsigned char fg[4] = {226, 236, 232, 255};
    unsigned char hi[4] = {92, 214, 170, 255};
    float x = 10.0f, y = 10.0f;
    drawRect(x, y, std::min(900.0f, (float)winW_ - 20.0f), 42.0f, bg, 0.52f);
    drawText(x + 12.0f, y + 13.0f, "Tab/H: UI", hi, 0.95f, 1.35f);
    float cx = x + 118.0f;
    for (const auto& id : pinned_) {
        int idx = controlIndexById(id);
        if (idx < 0) continue;
        std::string label = controls_[idx].label + " " + valueText(idx);
        float cw = std::min(210.0f, 24.0f + (float)label.size() * 9.2f);
        if (cx + cw > winW_ - 10.0f) break;
        drawRect(cx, y + 7.0f, cw, 28.0f, hi, 0.15f);
        drawText(cx + 9.0f, y + 14.0f, label, fg, 0.78f, 1.15f);
        cx += cw + 6.0f;
    }
}

void UiPanel::drawFullPanel() {
    hits_.clear();
    unsigned char bg[4] = {5, 7, 8, 255};
    unsigned char panel[4] = {15, 19, 20, 255};
    unsigned char fg[4] = {230, 238, 235, 255};
    unsigned char dim[4] = {99, 110, 112, 255};
    unsigned char hi[4] = {92, 214, 170, 255};

    float margin = 18.0f;
    float pw = std::min(1360.0f, (float)winW_ - margin * 2.0f);
    float ph = std::min(860.0f, (float)winH_ - margin * 2.0f);
    float x = margin, y = margin;
    drawRect(x, y, pw, ph, bg, 0.78f);
    drawOutline(x, y, pw, ph, hi, 0.42f);
    drawText(x + 20.0f, y + 18.0f, title_, fg, 0.96f, 2.1f);
    float winBtnW = 112.0f;
    drawRect(x + pw - winBtnW - 18.0f, y + 16.0f, winBtnW, 32.0f, hi, 0.18f);
    drawOutline(x + pw - winBtnW - 18.0f, y + 16.0f, winBtnW, 32.0f, hi, 0.55f);
    drawText(x + pw - winBtnW - 8.0f, y + 26.0f, "Windowed", fg, 0.94f, 1.05f);
    hits_.push_back({HitKind::Windowed, -1, x + pw - winBtnW - 18.0f, y + 16.0f, winBtnW, 32.0f, 0.0f, 0.0f});
    drawText(x + pw - 520.0f, y + 24.0f, "[] sections  Enter exact  P pin  Tab/Esc close", dim, 0.82f, 1.15f);

    float tabX = x + 18.0f;
    float tabY = y + 70.0f;
    float tabW = 172.0f;
    for (int i = 0; i < (int)sections_.size(); i++) {
        bool active = i == activeSection_;
        drawRect(tabX, tabY + i * 48.0f, tabW, 38.0f, active ? hi : panel, active ? 0.34f : 0.72f);
        drawText(tabX + 12.0f, tabY + i * 48.0f + 13.0f, sections_[i].label,
                 active ? fg : dim, active ? 0.95f : 0.8f, 1.35f);
        hits_.push_back({HitKind::Section, i, tabX, tabY + i * 48.0f, tabW, 38.0f, 0.0f, 0.0f});
    }

    float listX = x + 216.0f;
    float listY = y + 70.0f;
    float listW = pw * 0.52f;
    auto rows = activeControlIndices();
    if (selectedControl_ < 0 && !rows.empty()) selectedControl_ = rows.front();
    if (activeSectionIsPinned()) {
        drawText(listX, listY - 24.0f, "drag :: handles to reorder pinned controls", dim, 0.84f, 1.1f);
    }
    for (int r = 0; r < (int)rows.size(); r++) {
        int idx = rows[r];
        float ry = listY + r * 48.0f;
        if (ry + 41.0f > y + ph - 24.0f) break;
        if (controls_[idx].kind == UiControlKind::Toggle) drawToggleRow(idx, listX, ry, listW, false);
        else drawSliderRow(idx, listX, ry, listW, false);
    }

    float sideX = listX + listW + 20.0f;
    float sideW = x + pw - sideX - 18.0f;
    drawContextPanel(sideX, listY, sideW, y + ph - listY - 18.0f);
}

void UiPanel::draw() {
    if (!prog_ || winW_ <= 0 || winH_ <= 0) return;
    GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST);
    GLboolean blendWas = glIsEnabled(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (visible_) drawFullPanel();
    else {
        hits_.clear();
        drawDock();
    }
    if (depthWas) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    if (blendWas) glEnable(GL_BLEND);
    else glDisable(GL_BLEND);
}
