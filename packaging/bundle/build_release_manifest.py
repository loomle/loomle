#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
from typing import Optional


def load_manifest(path: Optional[Path]) -> dict:
    if path is None or not path.exists():
        return {"latest": "", "versions": {}}
    return json.loads(path.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Build or merge a LOOMLE release manifest.")
    parser.add_argument("--manifest-path", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--asset-url", required=True)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--client-binary-relpath", required=True)
    parser.add_argument("--client-sha256", required=True)
    parser.add_argument("--plugin-cache-source", default="plugin-cache/LoomleBridge")
    parser.add_argument("--base-manifest")
    args = parser.parse_args()

    manifest_path = Path(args.manifest_path)
    base_manifest = Path(args.base_manifest) if args.base_manifest else None
    manifest = load_manifest(base_manifest)

    manifest["latest"] = args.version
    versions = manifest.setdefault("versions", {})
    version_entry = versions.setdefault(args.version, {})
    packages = version_entry.setdefault("packages", {})
    package = {
        "url": args.asset_url,
        "sha256": args.sha256,
        "format": "zip",
        "client_binary_relpath": args.client_binary_relpath,
        "client_sha256": args.client_sha256,
        "install": {
            "global": {
                "clientBinary": args.client_binary_relpath,
                "pluginCache": {
                    "source": args.plugin_cache_source,
                    "destination": "plugin-cache/LoomleBridge",
                },
            },
        },
    }
    packages[args.platform] = package

    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
