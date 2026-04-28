#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
from pathlib import Path


def replace_glsl_version(path: Path) -> None:
    text = path.read_text()
    text = text.replace("#version 460 core", "#version 450 core", 1)
    path.write_text(text)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("resources_dir", type=Path)
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    resources_dir = args.resources_dir.resolve()
    if resources_dir.exists():
        shutil.rmtree(resources_dir)
    resources_dir.mkdir(parents=True, exist_ok=True)

    shutil.copytree(repo_root / "shaders", resources_dir / "shaders")
    shutil.copytree(repo_root / "presets", resources_dir / "presets")
    shutil.copytree(repo_root / "js",      resources_dir / "js")
    shutil.copytree(repo_root / "music",   resources_dir / "music")

    replace_glsl_version(resources_dir / "shaders" / "main.vert")
    replace_glsl_version(resources_dir / "shaders" / "main.frag")
    replace_glsl_version(resources_dir / "shaders" / "blit.frag")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
