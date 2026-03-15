#!/usr/bin/env python3
import argparse
import json
import shutil
import sys
from pathlib import Path

import os

EDITOR_PERF_SECTION = "[/Script/UnrealEd.EditorPerformanceSettings]"
EDITOR_THROTTLE_SETTING = "bThrottleCPUWhenNotForeground=False"
WORKSPACE_SOURCE_ROOT = Path("workspace/Loomle")
PLUGIN_SOURCE_ROOT = Path("plugin/LoomleBridge")


def fail(message: str) -> None:
    print(f"[FAIL] {message}", file=sys.stderr)
    raise SystemExit(1)


def load_manifest(path: Path) -> dict:
    if not path.exists():
        fail(f"manifest not found: {path}")
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        fail(f"failed to read manifest {path}: {exc}")
    if not isinstance(data, dict):
        fail(f"manifest root must be an object: {path}")
    return data


def resolve_package_entry(manifest: dict, version: str, platform: str) -> dict:
    versions = manifest.get("versions")
    if not isinstance(versions, dict):
        fail("manifest missing versions object")
    version_entry = versions.get(version)
    if not isinstance(version_entry, dict):
        fail(f"manifest missing version entry: {version}")
    packages = version_entry.get("packages")
    if not isinstance(packages, dict):
        fail(f"manifest version entry missing packages: {version}")
    package = packages.get(platform)
    if not isinstance(package, dict):
        fail(f"manifest missing platform package: version={version} platform={platform}")
    return package


def copy_tree(source: Path, destination: Path) -> None:
    if not source.exists():
        fail(f"install source not found: {source}")
    if destination.exists():
        shutil.rmtree(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, destination)


def require_existing_file(path: Path, label: str) -> None:
    if not path.is_file():
        fail(f"{label} not found: {path}")


def require_uproject(project_root: Path) -> None:
    if any(path.is_file() and path.suffix.lower() == ".uproject" for path in project_root.iterdir()):
        return
    fail(f"no .uproject found under: {project_root}")


def ensure_ini_section_setting(path: Path, section: str, setting: str) -> None:
    existing = ""
    if path.exists():
        try:
            existing = path.read_text(encoding="utf-8")
        except Exception as exc:
            fail(f"failed to read editor settings {path}: {exc}")

    in_section = False
    for line in existing.splitlines():
        trimmed = line.strip()
        if trimmed.startswith("[") and trimmed.endswith("]"):
            in_section = trimmed.lower() == section.lower()
            continue
        if in_section and trimmed.lower() == setting.lower():
            return

    updated = existing.rstrip("\r\n")
    if updated:
        updated += "\n\n"
    updated += f"{section}\n{setting}\n"

    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        path.write_text(updated, encoding="utf-8")
    except Exception as exc:
        fail(f"failed to write editor settings {path}: {exc}")


def resolve_installed_path(
    *,
    project_root: Path,
    source_root: Path,
    destination_root: str,
    bundle_relative_path: str,
) -> Path:
    try:
        relative_path = Path(bundle_relative_path).relative_to(source_root)
    except ValueError:
        fail(
            "bundle path does not live under expected source root: "
            f"path={bundle_relative_path} source_root={source_root}"
        )
    return project_root / destination_root / relative_path


def write_runtime_install_state(
    *,
    project_root: Path,
    version: str,
    platform: str,
    plugin_mode: str,
    plugin_destination_root: str,
    workspace_destination_root: str,
    server_binary_relpath: str,
    client_binary_relpath: str,
) -> Path:
    runtime_dir = project_root / workspace_destination_root / "runtime"
    runtime_dir.mkdir(parents=True, exist_ok=True)
    install_state_path = runtime_dir / "install.json"
    editor_settings_path = project_root / "Config" / "DefaultEditorSettings.ini"

    install_state = {
        "schemaVersion": 1,
        "installedVersion": version,
        "platform": platform,
        "pluginMode": plugin_mode,
        "projectRoot": str(project_root),
        "workspaceRoot": str(project_root / workspace_destination_root),
        "pluginRoot": str(project_root / plugin_destination_root),
        "clientPath": str(
            resolve_installed_path(
                project_root=project_root,
                source_root=WORKSPACE_SOURCE_ROOT,
                destination_root=workspace_destination_root,
                bundle_relative_path=client_binary_relpath,
            )
        ),
        "serverPath": str(
            resolve_installed_path(
                project_root=project_root,
                source_root=PLUGIN_SOURCE_ROOT,
                destination_root=plugin_destination_root,
                bundle_relative_path=server_binary_relpath,
            )
        ),
        "editorPerformance": {
            "settingsFile": str(editor_settings_path),
            "throttleWhenNotForeground": False,
        },
    }
    try:
        install_state_path.write_text(json.dumps(install_state, indent=2) + "\n", encoding="utf-8")
    except Exception as exc:
        fail(f"failed to write install state {install_state_path}: {exc}")
    return install_state_path


def plugin_binary_platform_dir(platform: str) -> str | None:
    return {
        "darwin": "Mac",
        "linux": "Linux",
        "windows": "Win64",
    }.get(platform)


def strip_plugin_source_for_precompiled_install(*, plugin_root: Path, platform: str, plugin_mode: str) -> None:
    if plugin_mode != "prebuilt":
        return

    source_dir = plugin_root / "Source"
    if not source_dir.is_dir():
        return

    binary_platform_dir = plugin_binary_platform_dir(platform)
    if not binary_platform_dir:
        return

    if not (plugin_root / "Binaries" / binary_platform_dir).is_dir():
        return

    try:
        shutil.rmtree(source_dir)
    except Exception as exc:
        fail(f"failed to remove plugin source for precompiled install {source_dir}: {exc}")


def ensure_executable_file(path: Path) -> None:
    if os.name == "nt":
        return
    try:
        mode = path.stat().st_mode
        path.chmod(mode | 0o755)
    except Exception as exc:
        fail(f"failed to mark executable {path}: {exc}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Install a LOOMLE release bundle into a user project.")
    parser.add_argument("--bundle-root", required=True, help="Extracted release bundle root")
    parser.add_argument("--project-root", required=True, help="Destination Unreal project root")
    parser.add_argument("--manifest-path", required=True, help="Release manifest path")
    parser.add_argument("--platform", required=True, help="Package platform key, e.g. darwin/linux/windows")
    parser.add_argument("--version", default="", help="Version to install; defaults to manifest.latest")
    parser.add_argument(
        "--plugin-mode",
        default="prebuilt",
        choices=["prebuilt", "source"],
        help="Install the plugin in prebuilt mode (default) or keep Source/ for local recompiles.",
    )
    args = parser.parse_args()

    bundle_root = Path(args.bundle_root).resolve()
    project_root = Path(args.project_root).resolve()
    manifest_path = Path(args.manifest_path).resolve()

    if not bundle_root.exists():
        fail(f"bundle root not found: {bundle_root}")
    if not project_root.exists():
        fail(f"project root not found: {project_root}")
    require_uproject(project_root)

    manifest = load_manifest(manifest_path)
    version = args.version or manifest.get("latest")
    if not isinstance(version, str) or not version:
        fail("manifest latest version is missing and --version was not supplied")

    package = resolve_package_entry(manifest, version, args.platform)
    install = package.get("install")
    if not isinstance(install, dict):
        fail("package missing install object")

    plugin_install = install.get("plugin")
    workspace_install = install.get("workspace")
    if not isinstance(plugin_install, dict) or not isinstance(workspace_install, dict):
        fail("install object must include plugin and workspace entries")

    plugin_source = bundle_root / str(plugin_install.get("source", ""))
    plugin_destination = project_root / str(plugin_install.get("destination", ""))
    workspace_source = bundle_root / str(workspace_install.get("source", ""))
    workspace_destination = project_root / str(workspace_install.get("destination", ""))
    server_binary_relpath = package.get("server_binary_relpath")
    client_binary_relpath = package.get("client_binary_relpath")

    if not str(plugin_install.get("destination", "")).startswith("Plugins/"):
        fail(f"plugin destination must stay under Plugins/: {plugin_install}")
    if str(workspace_install.get("destination", "")) != "Loomle":
        fail(f"workspace destination must be Loomle: {workspace_install}")
    if not isinstance(server_binary_relpath, str) or not server_binary_relpath:
        fail("package missing server_binary_relpath")
    if not isinstance(client_binary_relpath, str) or not client_binary_relpath:
        fail("package missing client_binary_relpath")

    require_existing_file(bundle_root / server_binary_relpath, "server binary")
    require_existing_file(bundle_root / client_binary_relpath, "client binary")

    copy_tree(plugin_source, plugin_destination)
    copy_tree(workspace_source, workspace_destination)
    strip_plugin_source_for_precompiled_install(
        plugin_root=plugin_destination,
        platform=args.platform,
        plugin_mode=args.plugin_mode,
    )
    ensure_executable_file(
        resolve_installed_path(
            project_root=project_root,
            source_root=PLUGIN_SOURCE_ROOT,
            destination_root=str(plugin_install.get("destination", "")),
            bundle_relative_path=server_binary_relpath,
        )
    )
    ensure_executable_file(
        resolve_installed_path(
            project_root=project_root,
            source_root=WORKSPACE_SOURCE_ROOT,
            destination_root=str(workspace_install.get("destination", "")),
            bundle_relative_path=client_binary_relpath,
        )
    )
    ensure_ini_section_setting(
        project_root / "Config" / "DefaultEditorSettings.ini",
        EDITOR_PERF_SECTION,
        EDITOR_THROTTLE_SETTING,
    )
    install_state_path = write_runtime_install_state(
        project_root=project_root,
        version=version,
        platform=args.platform,
        plugin_mode=args.plugin_mode,
        plugin_destination_root=str(plugin_install.get("destination", "")),
        workspace_destination_root=str(workspace_install.get("destination", "")),
        server_binary_relpath=server_binary_relpath,
        client_binary_relpath=client_binary_relpath,
    )

    result = {
        "installedVersion": version,
        "platform": args.platform,
        "pluginMode": args.plugin_mode,
        "bundleRoot": str(bundle_root),
        "projectRoot": str(project_root),
        "plugin": {
            "source": str(plugin_source),
            "destination": str(plugin_destination),
        },
        "workspace": {
            "source": str(workspace_source),
            "destination": str(workspace_destination),
        },
        "runtime": {
            "installState": str(install_state_path),
        },
    }
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
