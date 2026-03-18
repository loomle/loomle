#!/usr/bin/env python3
import argparse
import zipfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Write a release bundle zip with normalized forward-slash entry names.")
    parser.add_argument("--bundle-dir", required=True)
    parser.add_argument("--archive-path", required=True)
    args = parser.parse_args()

    bundle_dir = Path(args.bundle_dir).resolve()
    archive_path = Path(args.archive_path).resolve()
    archive_path.parent.mkdir(parents=True, exist_ok=True)

    with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(bundle_dir.rglob("*")):
            if path.is_dir():
                continue
            relative = path.relative_to(bundle_dir).as_posix()
            archive.write(path, arcname=relative)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
