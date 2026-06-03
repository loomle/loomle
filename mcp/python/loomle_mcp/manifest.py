from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any


class ManifestError(RuntimeError):
    """Raised when the LOOMLE tool manifest is missing or malformed."""


@dataclass(frozen=True)
class ToolManifest:
    path: Path
    data: dict[str, Any]

    @property
    def tools(self) -> list[dict[str, Any]]:
        tools = self.data.get("tools")
        if not isinstance(tools, list):
            raise ManifestError("manifest.tools must be an array")
        return tools

    def tools_for(self, target: str) -> list[dict[str, Any]]:
        return [
            tool
            for tool in self.tools
            if target in tool.get("availability", [])
        ]

    def list_tools(self, target: str = "python") -> list[dict[str, Any]]:
        tools = []
        for tool in self.tools_for(target):
            listed_tool = {
                "name": tool["name"],
                "description": tool["description"],
                "inputSchema": thin_input_schema(tool["name"], tool["inputSchema"]),
            }
            tools.append(listed_tool)
        return tools

    def get_tool(self, name: str) -> dict[str, Any] | None:
        for tool in self.tools:
            if tool.get("name") == name:
                return tool
        return None

    def inspect_schema(
        self,
        *,
        domain: str,
        tool_name: str,
        operation: str | None = None,
        include: list[str] | None = None,
    ) -> dict[str, Any]:
        includes = include or ["summary", "operation"]
        self._validate_includes(includes)
        tool = self.get_tool(tool_name)
        if tool is None:
            raise ManifestError(f"unknown schema.inspect tool: {tool_name}")

        if not self._tool_matches_domain(tool, domain):
            raise ManifestError(
                f"tool {tool_name} does not belong to schema domain {domain}"
            )

        schema_inspect = tool.get("schemaInspect")
        if not isinstance(schema_inspect, dict):
            if operation is not None:
                raise ManifestError(f"tool does not support operation schema.inspect: {tool_name}")
            payload: dict[str, Any] = {
                "domain": domain,
                "tool": tool_name,
                "operations": [],
            }
            self._append_input_schema(payload, tool, includes)
            self._append_output_schema(payload, tool, includes)
            return payload

        operations = schema_inspect.get("operations")
        if not isinstance(operations, list):
            raise ManifestError(f"{tool_name}.schemaInspect.operations must be an array")

        if operation is None:
            payload = {
                "domain": domain,
                "tool": tool_name,
                "operations": [
                    {
                        "name": op["name"],
                        "category": op["category"],
                        "summary": op["summary"],
                    }
                    for op in operations
                ],
                "source": schema_inspect.get("source", {}),
            }
            self._append_input_schema(payload, tool, includes)
            self._append_output_schema(payload, tool, includes)
            return payload

        for op in operations:
            if op.get("name") == operation:
                payload: dict[str, Any] = {
                    "domain": domain,
                    "tool": tool_name,
                    "operation": operation,
                    "category": op["category"],
                }
                if "summary" in includes:
                    payload["summary"] = op["summary"]
                if "operation" in includes:
                    payload["operationSchema"] = op["schema"]
                if "examples" in includes:
                    payload["examples"] = op.get("examples", [])
                if "errors" in includes:
                    payload["errors"] = op.get("errors", [])
                if "notes" in includes:
                    payload["notes"] = op.get("notes", [])
                self._append_input_schema(payload, tool, includes)
                self._append_output_schema(payload, tool, includes)
                return payload

        available = [op.get("name") for op in operations]
        raise ManifestError(
            f"unknown operation for {tool_name}: {operation}; available: {available}"
        )

    def _tool_matches_domain(self, tool: dict[str, Any], domain: str) -> bool:
        schema_inspect = tool.get("schemaInspect")
        if isinstance(schema_inspect, dict) and schema_inspect.get("domain") == domain:
            return True
        name = tool.get("name")
        return isinstance(name, str) and (
            name == domain
            or name.startswith(f"{domain}.")
            or name.startswith(f"{domain}_")
        )

    def _validate_includes(self, includes: list[str]) -> None:
        allowed = {"summary", "input", "operation", "examples", "errors", "notes", "output"}
        unsupported = [include for include in includes if include not in allowed]
        if unsupported:
            raise ManifestError(
                f"unsupported schema.inspect include: {unsupported[0]}; "
                f"available: {sorted(allowed)}"
            )

    def _append_output_schema(
        self,
        payload: dict[str, Any],
        tool: dict[str, Any],
        includes: list[str],
    ) -> None:
        if "output" not in includes:
            return
        if "outputSchema" in tool:
            payload["hasOutputSchema"] = True
            payload["outputSchema"] = tool["outputSchema"]
        else:
            payload["hasOutputSchema"] = False

    def _append_input_schema(
        self,
        payload: dict[str, Any],
        tool: dict[str, Any],
        includes: list[str],
    ) -> None:
        if "input" not in includes:
            return
        if "inputSchema" in tool:
            payload["hasInputSchema"] = True
            payload["inputSchema"] = tool["inputSchema"]
        else:
            payload["hasInputSchema"] = False


def thin_input_schema(name: str, full_schema: dict[str, Any]) -> dict[str, Any]:
    if name == "schema_inspect":
        return full_schema

    schema: dict[str, Any] = {"type": "object"}
    required = full_schema.get("required")
    if isinstance(required, list):
        schema["required"] = required
    properties = full_schema.get("properties")
    if not isinstance(properties, dict):
        return schema

    required_names = {item for item in required or [] if isinstance(item, str)}
    thin_properties: dict[str, Any] = {}
    for prop_name, prop_schema in properties.items():
        if prop_name in required_names or _high_signal_optional(prop_name):
            thin_properties[prop_name] = _thin_property(prop_schema)
    if thin_properties or "properties" in full_schema:
        schema["properties"] = thin_properties
    return schema


def _high_signal_optional(prop_name: str) -> bool:
    return prop_name in {
        "kind",
        "view",
        "operation",
        "memberKind",
        "action",
        "fn",
        "tool",
        "domain",
        "dryRun",
        "expectedRevision",
    }


def _thin_property(prop_schema: Any) -> dict[str, Any]:
    if not isinstance(prop_schema, dict):
        return {}
    thin: dict[str, Any] = {}
    for field in ("type", "enum", "const", "minLength", "default"):
        if field in prop_schema:
            thin[field] = prop_schema[field]
    description = prop_schema.get("description")
    if isinstance(description, str):
        thin["description"] = _short_description(description)
    if not thin:
        thin["type"] = "object"
    return thin


def _short_description(description: str) -> str:
    limit = 120
    if len(description) <= limit:
        return description
    return description[:limit].rstrip() + "..."


def default_manifest_path() -> Path:
    package_root = Path(__file__).resolve().parents[1]
    candidates = [
        package_root / "tool-manifest" / "manifest.json",
        package_root.parent / "manifest" / "manifest.json",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[-1]


def load_manifest(path: str | Path | None = None) -> ToolManifest:
    manifest_path = Path(path) if path is not None else default_manifest_path()
    manifest_path = manifest_path.resolve()
    try:
        with manifest_path.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
    except FileNotFoundError as exc:
        raise ManifestError(f"tool manifest not found: {manifest_path}") from exc
    except json.JSONDecodeError as exc:
        raise ManifestError(f"invalid tool manifest JSON at {manifest_path}: {exc}") from exc

    if not isinstance(data, dict):
        raise ManifestError("tool manifest root must be an object")
    if data.get("product") != "loomle":
        raise ManifestError('tool manifest product must be "loomle"')
    return ToolManifest(path=manifest_path, data=data)
