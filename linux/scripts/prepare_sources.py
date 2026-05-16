#!/usr/bin/env python3
"""Linux-side source transforms over the Windows-first root tree.

Most of the historical transforms here became obsolete as the root
source learned to be cross-platform on its own (inline `#ifdef _WIN32`
/ `#ifdef __APPLE__` guards, a cross-platform `camera.h`, shaders
already at `#version 410`). The single remaining real-world need is
the GL context-minor hint: root requests GL 4.6 on non-Apple builds,
but linux drivers are more reliable at 4.5.

See `linux/CLEANUP.md` for the path to delete this script entirely and
have the linux Makefile compile root sources directly, mirroring the
macOS architecture.
"""
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
        '    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);\n'
        '#ifdef __APPLE__\n'
        '    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);\n'
        '    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);\n'
        '#else\n'
        '    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);\n'
        '#endif\n',
        '    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);\n'
        '#ifdef __APPLE__\n'
        '    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);\n'
        '    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);\n'
        '#else\n'
        '    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);\n'
        '#endif\n',
        "gl context minor version (linux: 4.5 instead of 4.6)",
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
    (out_dir / "main.cpp").write_text(transform_main(main_src))

    # camera.h and overlay.cpp are now cross-platform in root; copy unchanged
    # so the Makefile's GEN_DIR include path keeps resolving.
    (out_dir / "camera.h").write_text((repo_root / "camera.h").read_text())
    (out_dir / "overlay.cpp").write_text((repo_root / "overlay.cpp").read_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
