#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


CLAUDE_TOOL_NAME_RE = re.compile(r"^[a-zA-Z0-9_-]{1,64}$")
CLAUDE_RESERVED_SERVER_NAMES = {"workspace"}


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


def validate_claude_server_name(server_name: str) -> None:
    if not CLAUDE_TOOL_NAME_RE.fullmatch(server_name):
        fail(
            f"Claude MCP server name {server_name!r} must match "
            r"^[a-zA-Z0-9_-]{1,64}$"
        )
    if server_name in CLAUDE_RESERVED_SERVER_NAMES:
        fail(f"Claude MCP server name {server_name!r} is reserved")


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
        if not CLAUDE_TOOL_NAME_RE.fullmatch(name):
            fail(
                f"{name}.name must be Claude-safe: "
                r"^[a-zA-Z0-9_-]{1,64}$"
            )
        if name in CLAUDE_RESERVED_SERVER_NAMES:
            fail(f"{name}.name must not use a Claude reserved MCP server name")
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

        schema_hints = tool.get("schemaHints")
        if schema_hints is not None:
            schema_hints = require_array(schema_hints, f"{name}.schemaHints")
            if not schema_hints:
                fail(f"{name}.schemaHints must not be empty")
            for hint_index, raw_hint in enumerate(schema_hints):
                hint = require_object(raw_hint, f"{name}.schemaHints[{hint_index}]")
                if hint.get("purpose") != "operation_schema":
                    fail(f"{name}.schemaHints[{hint_index}].purpose must be operation_schema")
                if hint.get("schemaTool") != "schema_inspect":
                    fail(f"{name}.schemaHints[{hint_index}].schemaTool must be schema_inspect")
                if hint.get("tool") != name:
                    fail(f"{name}.schemaHints[{hint_index}].tool must match the tool name")
                if not isinstance(hint.get("domain"), str) or not hint["domain"]:
                    fail(f"{name}.schemaHints[{hint_index}].domain must be a non-empty string")
                has_operation = isinstance(hint.get("operation"), str) and bool(hint["operation"])
                has_operation_from = isinstance(hint.get("operationFrom"), str) and bool(
                    hint["operationFrom"]
                )
                if has_operation == has_operation_from:
                    fail(
                        f"{name}.schemaHints[{hint_index}] must set exactly one of "
                        "operation or operationFrom"
                    )
                include = hint.get("include")
                if include is not None:
                    include = require_array(include, f"{name}.schemaHints[{hint_index}].include")
                    allowed_include = {
                        "summary",
                        "input",
                        "operation",
                        "examples",
                        "errors",
                        "notes",
                        "output",
                    }
                    for item in include:
                        if item not in allowed_include:
                            fail(
                                f"{name}.schemaHints[{hint_index}].include contains "
                                f"unsupported section: {item!r}"
                            )

        dispatch = require_object(tool.get("dispatch"), f"{name}.dispatch")
        dispatch_kind = dispatch.get("kind")
        if dispatch_kind not in {"local", "bridgeRpc", "unavailable"}:
            fail(f"{name}.dispatch.kind is unsupported: {dispatch_kind!r}")
        if dispatch_kind == "bridgeRpc" and not dispatch.get("tool"):
            fail(f"{name}.dispatch.tool is required for bridgeRpc")

        schema_inspect = tool.get("schemaInspect")
        if schema_inspect is not None:
            if schema_hints is None:
                fail(f"{name}.schemaInspect tools must declare schemaHints")
            schema_inspect = require_object(schema_inspect, f"{name}.schemaInspect")
            domain = schema_inspect.get("domain")
            schema_tool = schema_inspect.get("tool")
            if not isinstance(domain, str) or not domain:
                fail(f"{name}.schemaInspect.domain must be a non-empty string")
            if schema_tool != name:
                fail(f"{name}.schemaInspect.tool must match the public tool name")
            for hint_index, hint in enumerate(schema_hints):
                if hint.get("domain") != domain:
                    fail(
                        f"{name}.schemaHints[{hint_index}].domain must match "
                        "schemaInspect.domain"
                    )
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
    parser.add_argument(
        "--claude-server-name",
        default="loomle",
        help="MCP server name to validate against Claude Code naming rules.",
    )
    args = parser.parse_args()

    validate_claude_server_name(args.claude_server_name)
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
