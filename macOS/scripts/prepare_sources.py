#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise SystemExit(f"prepare_sources.py: couldn't find expected snippet for {label}")
    return text.replace(old, new, 1)


MAC_RUNTIME_BLOCK = r'''

#ifdef __APPLE__
static std::string g_user_base = "";

static std::string ensure_trailing_slash(std::string s) {
    if (!s.empty() && s.back() != '/') s.push_back('/');
    return s;
}

static std::string mac_executable_path() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size + 1, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return "";
    char resolved[PATH_MAX];
    if (!realpath(buf.c_str(), resolved)) return "";
    return resolved;
}

static std::string mac_executable_dir() {
    std::string exe = mac_executable_path();
    size_t slash = exe.find_last_of('/');
    return slash == std::string::npos ? std::string() : ensure_trailing_slash(exe.substr(0, slash));
}

static std::string mac_asset_base() {
    std::string exeDir = mac_executable_dir();
    const std::string marker = ".app/Contents/MacOS/";
    size_t pos = exeDir.find(marker);
    if (pos != std::string::npos)
        return exeDir.substr(0, pos + 5) + "/Contents/Resources/";
    return exeDir;
}

static std::string mac_user_base() {
    const char* home = std::getenv("HOME");
    if (home && home[0])
        return ensure_trailing_slash(std::string(home) + "/Library/Application Support/Crutchfield Machine");
    return mac_executable_dir();
}

static void seed_user_tree(const std::string& assetBase, const char* name) {
    fs::path src = fs::path(assetBase) / name;
    fs::path dst = fs::path(name);
    std::error_code ec;
    if (!fs::exists(src)) return;
    fs::create_directories(dst, ec);
    if (ec) return;
    for (const auto& e : fs::recursive_directory_iterator(src)) {
        if (!e.is_regular_file()) continue;
        fs::path rel = fs::relative(e.path(), src, ec);
        if (ec) continue;
        fs::path out = dst / rel;
        fs::create_directories(out.parent_path(), ec);
        if (ec) continue;
        if (!fs::exists(out)) fs::copy_file(e.path(), out, fs::copy_options::skip_existing, ec);
    }
}

static void bootstrap_macos_runtime() {
    g_shader_base = mac_asset_base();
    g_user_base   = mac_user_base();
    std::error_code ec;
    fs::create_directories(g_user_base, ec);
    if (!ec && chdir(g_user_base.c_str()) == 0) {
        seed_user_tree(g_shader_base, "presets");
        seed_user_tree(g_shader_base, "js");
        seed_user_tree(g_shader_base, "music");
        seed_user_tree(g_shader_base, "samples");
    } else {
        std::fprintf(stderr, "[paths] warning: couldn't use %s as runtime dir\n",
                     g_user_base.c_str());
    }
}
#endif
'''


def transform_main(text: str) -> str:
    text = replace_once(
        text,
        '#include <GLFW/glfw3.h>\n#ifdef _WIN32\n',
        '#include <GLFW/glfw3.h>\n#ifdef __APPLE__\n'
        '  #include <mach-o/dyld.h>\n'
        '  #include <unistd.h>\n'
        '#endif\n'
        '#ifdef _WIN32\n',
        "apple includes",
    )
    text = replace_once(
        text,
        '#include <cmath>\n#include <string>\n',
        '#include <cmath>\n#include <limits.h>\n#include <string>\n',
        "limits include",
    )
    text = replace_once(
        text,
        '      "Usage: feedback.exe [options]\\n"\n',
        '      "Usage: feedback [options]\\n"\n',
        "help usage",
    )
    text = replace_once(
        text,
        '      "Launch with NO arguments to get an interactive mode picker — handy for\\n"\n'
        '      "non-CLI use and for double-clicking the exe from Explorer.\\n\\n"\n',
        '      "On Windows only: launch with NO arguments to get an interactive mode\\n"\n'
        '      "picker for double-click / non-CLI use.\\n\\n"\n',
        "help picker note",
    )
    text = replace_once(
        text,
        'namespace fs = std::filesystem;\n',
        'namespace fs = std::filesystem;\n' + MAC_RUNTIME_BLOCK + '\n',
        "mac runtime block",
    )
    text = replace_once(
        text,
        'static std::string preset_dir() {\n'
        '    return g_shader_base.empty() ? "presets" : (g_shader_base + "presets");\n'
        '}\n',
        'static std::string preset_dir() {\n'
        '#ifdef __APPLE__\n'
        '    return "presets";\n'
        '#else\n'
        '    return g_shader_base.empty() ? "presets" : (g_shader_base + "presets");\n'
        '#endif\n'
        '}\n',
        "preset_dir",
    )
    text = replace_once(
        text,
        'static std::string screenshot_dir() {\n'
        '    return g_shader_base.empty() ? "screenshots" : (g_shader_base + "screenshots");\n'
        '}\n',
        'static std::string screenshot_dir() {\n'
        '#ifdef __APPLE__\n'
        '    return "screenshots";\n'
        '#else\n'
        '    return g_shader_base.empty() ? "screenshots" : (g_shader_base + "screenshots");\n'
        '#endif\n'
        '}\n',
        "screenshot_dir",
    )
    text = replace_once(
        text,
        '    s += "Edit bindings.ini next to the exe to customize.\\n\\n";\n',
        '    s += "Edit bindings.ini in the app support folder to customize.\\n\\n";\n',
        "bindings help text",
    )
    text = replace_once(
        text,
        'int main(int argc, char** argv) {\n'
        '    g_cfg = parse_cli(argc, argv);\n',
        'int main(int argc, char** argv) {\n'
        '    g_cfg = parse_cli(argc, argv);\n'
        '#ifdef __APPLE__\n'
        '    bootstrap_macos_runtime();\n'
        '    if (!g_shader_base.empty()) printf("[paths] assets=%s\\n", g_shader_base.c_str());\n'
        '    if (!g_user_base.empty())   printf("[paths] user=%s\\n",   g_user_base.c_str());\n'
        '#endif\n',
        "main bootstrap",
    )
    text = replace_once(
        text,
        '    if (argc == 1) run_mode_picker(g_cfg);\n',
        '#ifdef _WIN32\n'
        '    if (argc == 1) run_mode_picker(g_cfg);\n'
        '#endif\n',
        "windows picker only",
    )
    text = replace_once(
        text,
        '    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);\n'
        '    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);\n'
        '    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);\n',
        '    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);\n'
        '#ifdef __APPLE__\n'
        '    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);\n'
        '    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);\n'
        '#else\n'
        '    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);\n'
        '#endif\n'
        '    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);\n',
        "gl context hints",
    )
    text = replace_once(
        text,
        '    {\n'
        '        std::string bindingsPath = g_shader_base.empty()\n'
        '            ? std::string("bindings.ini")\n'
        '            : (g_shader_base + "bindings.ini");\n'
        '        if (!g_input.loadIni(bindingsPath)) {\n',
        '    {\n'
        '#ifdef __APPLE__\n'
        '        std::string bindingsPath = "bindings.ini";\n'
        '#else\n'
        '        std::string bindingsPath = g_shader_base.empty()\n'
        '            ? std::string("bindings.ini")\n'
        '            : (g_shader_base + "bindings.ini");\n'
        '#endif\n'
        '        if (!g_input.loadIni(bindingsPath)) {\n',
        "bindings path",
    )
    return text


def transform_camera(text: str) -> str:
    text = replace_once(
        text,
        '// Minimal Media Foundation webcam → RGB buffer for use as a GL texture source.\n'
        '// Same interface as the Linux V4L2 camera so main.cpp is portable.\n',
        '// Platform webcam → RGB buffer for use as a GL texture source.\n'
        '// The interface stays fixed so main.cpp is portable across backends.\n',
        "camera comment",
    )
    text = replace_once(
        text,
        '    bool active() const { return reader_ != nullptr; }\n',
        '    bool active() const {\n'
        '#ifdef _WIN32\n'
        '        return reader_ != nullptr;\n'
        '#else\n'
        '        return impl_ != nullptr;\n'
        '#endif\n'
        '    }\n',
        "camera active",
    )
    text = replace_once(
        text,
        'private:\n'
        '    // Opaque pointers (avoid pulling Media Foundation headers into this file).\n'
        '    void* reader_ = nullptr;   // IMFSourceReader*\n'
        '    int   w_ = 0, h_ = 0;\n'
        '    uint32_t pixfmt_ = 0;      // fourcc of the negotiated format\n'
        '    bool     mf_started_ = false;\n'
        '};\n',
        'private:\n'
        '    // Windows Media Foundation state. Other platforms use a native backend.\n'
        '#ifdef _WIN32\n'
        '    void* reader_ = nullptr;   // IMFSourceReader*\n'
        '    int   w_ = 0, h_ = 0;\n'
        '    uint32_t pixfmt_ = 0;      // fourcc of the negotiated format\n'
        '    bool     mf_started_ = false;\n'
        '#else\n'
        '    void* impl_ = nullptr;     // platform-specific backend object\n'
        '    int   w_ = 0, h_ = 0;\n'
        '#endif\n'
        '};\n',
        "camera private block",
    )
    return text


def transform_overlay(text: str) -> str:
    text = replace_once(
        text,
        'static const char* VS = R"(#version 460 core\n',
        'static const char* VS = R"(#version 410 core\n',
        "overlay vertex version",
    )
    text = replace_once(
        text,
        'static const char* FS = R"(#version 460 core\n',
        'static const char* FS = R"(#version 410 core\n',
        "overlay fragment version",
    )
    return text


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("out_dir", type=Path)
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    main_src = (repo_root / "main.cpp").read_text()
    camera_src = (repo_root / "camera.h").read_text()

    overlay_src = (repo_root / "overlay.cpp").read_text()

    (out_dir / "main.cpp").write_text(transform_main(main_src))
    (out_dir / "camera.h").write_text(transform_camera(camera_src))
    (out_dir / "overlay.cpp").write_text(transform_overlay(overlay_src))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
