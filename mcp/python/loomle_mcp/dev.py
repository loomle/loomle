from __future__ import annotations

import argparse
import json
import sys
from typing import Any

from .manifest import ManifestError, load_manifest


def write_json(value: Any) -> None:
    json.dump(value, sys.stdout, ensure_ascii=False, indent=2)
    sys.stdout.write("\n")


def parse_include(values: list[str] | None) -> list[str] | None:
    if not values:
        return None
    includes: list[str] = []
    for value in values:
        includes.extend(part for part in value.split(",") if part)
    return includes or None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="LOOMLE Python MCP development helpers.")
    parser.add_argument("--manifest", help="Path to mcp/manifest/manifest.json.")

    action = parser.add_subparsers(dest="command", required=True)

    action.add_parser("list-tools", help="Print tools/list items generated from the manifest.")

    inspect = action.add_parser("schema-inspect", help="Print one schema.inspect response.")
    inspect.add_argument("--domain", required=True)
    inspect.add_argument("--tool", required=True, dest="tool_name")
    inspect.add_argument("--operation")
    inspect.add_argument(
        "--include",
        action="append",
        help="Comma-separated include set: summary,input,operation,examples,errors,notes,output.",
    )

    args = parser.parse_args(argv)

    try:
        manifest = load_manifest(args.manifest)
        if args.command == "list-tools":
            write_json({"tools": manifest.list_tools("python")})
            return 0
        if args.command == "schema-inspect":
            write_json(
                manifest.inspect_schema(
                    domain=args.domain,
                    tool_name=args.tool_name,
                    operation=args.operation,
                    include=parse_include(args.include),
                )
            )
            return 0
    except ManifestError as exc:
        print(f"[loomle-python-mcp][ERROR] {exc}", file=sys.stderr)
        return 1

    print(f"[loomle-python-mcp][ERROR] unsupported command: {args.command}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
