#!/usr/bin/env python3
import argparse
import json
import shutil
import sys
from pathlib import Path


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


def main() -> int:
    parser = argparse.ArgumentParser(description="Install a LOOMLE release bundle into a user project.")
    parser.add_argument("--bundle-root", required=True, help="Extracted release bundle root")
    parser.add_argument("--project-root", required=True, help="Destination Unreal project root")
    parser.add_argument("--manifest-path", required=True, help="Release manifest path")
    parser.add_argument("--platform", required=True, help="Package platform key, e.g. darwin/linux/windows")
    parser.add_argument("--version", default="", help="Version to install; defaults to manifest.latest")
    args = parser.parse_args()

    bundle_root = Path(args.bundle_root).resolve()
    project_root = Path(args.project_root).resolve()
    manifest_path = Path(args.manifest_path).resolve()

    if not bundle_root.exists():
        fail(f"bundle root not found: {bundle_root}")
    if not project_root.exists():
        fail(f"project root not found: {project_root}")

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

    result = {
        "installedVersion": version,
        "platform": args.platform,
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
    }
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
