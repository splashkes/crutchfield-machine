#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise SystemExit(f"prepare_sources.py: couldn't find expected snippet for {label}")
    return text.replace(old, new, 1)


def transform_main(text: str) -> str:
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
        '    if (argc == 1) run_mode_picker(g_cfg);\n',
        '#ifdef _WIN32\n'
        '    if (argc == 1) run_mode_picker(g_cfg);\n'
        '#endif\n',
        "windows picker only",
    )
    text = replace_once(
        text,
        '        "-f lavfi -i nullsrc=s=256x256:d=0.1 -c:v %s -f null NUL >nul 2>&1",\n',
        '        "-f lavfi -i nullsrc=s=256x256:d=0.1 -c:v %s -f null /dev/null >/dev/null 2>&1",\n',
        "ffmpeg probe null device",
    )
    text = replace_once(
        text,
        '    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);\n'
        '    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);\n',
        '    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);\n'
        '    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);\n',
        "gl context minor version",
    )
    return text


def transform_overlay(text: str) -> str:
    text = replace_once(
        text,
        'static const char* VS = R"(#version 460 core\n',
        'static const char* VS = R"(#version 450 core\n',
        "overlay vertex version",
    )
    text = replace_once(
        text,
        'static const char* FS = R"(#version 460 core\n',
        'static const char* FS = R"(#version 450 core\n',
        "overlay fragment version",
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
