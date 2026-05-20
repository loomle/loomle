#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def fail(message: str) -> None:
    print(f"[FAIL] {message}", file=sys.stderr)
    raise SystemExit(1)


def load_json(path: Path) -> object:
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except json.JSONDecodeError as exc:
        fail(f"{path}: invalid JSON: {exc}")
    except OSError as exc:
        fail(f"{path}: failed to read: {exc}")


def require_object(value: object, label: str) -> dict:
    if not isinstance(value, dict):
        fail(f"{label} must be an object")
    return value


def require_array(value: object, label: str) -> list:
    if not isinstance(value, list):
        fail(f"{label} must be an array")
    return value


def validate_manifest(manifest: dict) -> None:
    if manifest.get("schemaVersion") != 1:
        fail("manifest.schemaVersion must be 1")
    if manifest.get("product") != "loomle":
        fail('manifest.product must be "loomle"')

    tools = require_array(manifest.get("tools"), "manifest.tools")
    seen_tools: set[str] = set()
    schema_tools: set[tuple[str, str]] = set()

    for index, raw_tool in enumerate(tools):
        tool = require_object(raw_tool, f"tools[{index}]")
        name = tool.get("name")
        if not isinstance(name, str) or not name:
            fail(f"tools[{index}].name must be a non-empty string")
        if name in seen_tools:
            fail(f"duplicate tool name: {name}")
        seen_tools.add(name)

        description = tool.get("description")
        if not isinstance(description, str) or not description:
            fail(f"{name}.description must be a non-empty string")

        availability = require_array(tool.get("availability"), f"{name}.availability")
        for item in availability:
            if item not in {"native", "python"}:
                fail(f"{name}.availability contains unsupported target: {item!r}")

        if not isinstance(tool.get("requiresBridge"), bool):
            fail(f"{name}.requiresBridge must be a boolean")

        input_schema = require_object(tool.get("inputSchema"), f"{name}.inputSchema")
        if input_schema.get("type") != "object":
            fail(f"{name}.inputSchema.type must be object")

        dispatch = require_object(tool.get("dispatch"), f"{name}.dispatch")
        dispatch_kind = dispatch.get("kind")
        if dispatch_kind not in {"local", "bridgeRpc", "unavailable"}:
            fail(f"{name}.dispatch.kind is unsupported: {dispatch_kind!r}")
        if dispatch_kind == "bridgeRpc" and not dispatch.get("tool"):
            fail(f"{name}.dispatch.tool is required for bridgeRpc")

        schema_inspect = tool.get("schemaInspect")
        if schema_inspect is not None:
            schema_inspect = require_object(schema_inspect, f"{name}.schemaInspect")
            domain = schema_inspect.get("domain")
            schema_tool = schema_inspect.get("tool")
            if not isinstance(domain, str) or not domain:
                fail(f"{name}.schemaInspect.domain must be a non-empty string")
            if schema_tool != name:
                fail(f"{name}.schemaInspect.tool must match the public tool name")
            key = (domain, schema_tool)
            if key in schema_tools:
                fail(f"duplicate schema.inspect registration: {domain}/{schema_tool}")
            schema_tools.add(key)

            operations = require_array(
                schema_inspect.get("operations"),
                f"{name}.schemaInspect.operations",
            )
            seen_ops: set[str] = set()
            for op_index, raw_operation in enumerate(operations):
                operation = require_object(
                    raw_operation,
                    f"{name}.schemaInspect.operations[{op_index}]",
                )
                op_name = operation.get("name")
                if not isinstance(op_name, str) or not op_name:
                    fail(f"{name}.schemaInspect operation name must be non-empty")
                if op_name in seen_ops:
                    fail(f"{name}.schemaInspect has duplicate operation: {op_name}")
                seen_ops.add(op_name)
                require_object(operation.get("schema"), f"{name}.{op_name}.schema")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the LOOMLE tool manifest slice.")
    parser.add_argument(
        "--manifest",
        default="mcp/manifest/manifest.json",
        help="Path to the tool manifest JSON.",
    )
    args = parser.parse_args()

    manifest_path = Path(args.manifest)
    manifest = require_object(load_json(manifest_path), str(manifest_path))
    validate_manifest(manifest)
    print(
        f"tool manifest ok: {len(manifest['tools'])} tools "
        f"from {manifest_path}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
