#!/usr/bin/env python3
import argparse
import json
import shutil
import sys
from pathlib import Path


IGNORE_NAMES = {
    ".DS_Store",
    "__pycache__",
    ".pytest_cache",
    ".ruff_cache",
    ".mypy_cache",
    ".venv",
    "*.egg-info",
    "Binaries",
    "Intermediate",
    "Saved",
    "tests",
    "uv.lock",
}

FORBIDDEN_BINARY_SUFFIXES = {
    ".dll",
    ".dylib",
    ".exe",
    ".lib",
    ".pdb",
    ".so",
}


def fail(message: str) -> None:
    print(f"[FAIL] {message}", file=sys.stderr)
    raise SystemExit(1)


def reset_dir(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True, exist_ok=True)


def copy_tree(source: Path, destination: Path, ignore_names: set[str] | None = None) -> None:
    if not source.exists():
        fail(f"source not found: {source}")
    if destination.exists():
        shutil.rmtree(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    ignore = shutil.ignore_patterns(*(ignore_names or set()))
    shutil.copytree(source, destination, ignore=ignore)


def copy_file(source: Path, destination: Path) -> None:
    if not source.is_file():
        fail(f"file not found: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def ensure_filter_plugin(plugin_root: Path) -> None:
    filter_path = plugin_root / "Config" / "FilterPlugin.ini"
    filter_path.parent.mkdir(parents=True, exist_ok=True)
    if filter_path.exists():
        text = filter_path.read_text(encoding="utf-8")
    else:
        text = "[FilterPlugin]\n"
    line = "/Resources/MCP/...\n"
    if line.strip() not in {entry.strip() for entry in text.splitlines()}:
        if not text.endswith("\n"):
            text += "\n"
        text += line
        filter_path.write_text(text, encoding="utf-8")


def validate_fab_plugin(plugin_root: Path) -> None:
    required_files = [
        plugin_root / "LoomleBridge.uplugin",
        plugin_root / "Source" / "LoomleBridge" / "LoomleBridge.Build.cs",
        plugin_root / "Resources" / "MCP" / "pyproject.toml",
        plugin_root / "Resources" / "MCP" / "loomle_mcp_server.py",
        plugin_root / "Resources" / "MCP" / "loomle_mcp" / "server.py",
        plugin_root / "Resources" / "MCP" / "tool-manifest" / "manifest.json",
    ]
    missing = [str(path) for path in required_files if not path.is_file()]
    if missing:
        fail("Fab plugin staging is missing required files:\n" + "\n".join(missing))

    forbidden = []
    for path in plugin_root.rglob("*"):
        if any(part in {"Binaries", "Intermediate", "Saved"} for part in path.parts):
            forbidden.append(str(path))
        elif path.is_file() and path.suffix.lower() in FORBIDDEN_BINARY_SUFFIXES:
            forbidden.append(str(path))
    if forbidden:
        fail("Fab plugin source package contains platform binary/build outputs:\n" + "\n".join(forbidden))


def main() -> int:
    parser = argparse.ArgumentParser(description="Assemble a Fab-ready LoomleBridge plugin staging directory.")
    parser.add_argument("--repo-root", default=".")
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    output_dir = Path(args.output_dir).resolve()
    plugin_root = output_dir / "LoomleBridge"
    engine_plugin = repo_root / "engine" / "LoomleBridge"
    python_mcp = repo_root / "mcp" / "python"
    tool_manifest = repo_root / "mcp" / "manifest"

    reset_dir(output_dir)
    copy_tree(engine_plugin, plugin_root, IGNORE_NAMES)

    resources_mcp = plugin_root / "Resources" / "MCP"
    copy_tree(python_mcp, resources_mcp, IGNORE_NAMES)
    copy_tree(tool_manifest, resources_mcp / "tool-manifest", IGNORE_NAMES)
    copy_file(tool_manifest / "manifest.json", resources_mcp / "tool-manifest" / "manifest.json")
    ensure_filter_plugin(plugin_root)
    validate_fab_plugin(plugin_root)

    result = {
        "pluginRoot": str(plugin_root),
        "pythonMcp": str(resources_mcp),
        "toolManifest": str(resources_mcp / "tool-manifest" / "manifest.json"),
        "packageKind": "fab-source-plugin",
    }
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
