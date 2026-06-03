from __future__ import annotations

import argparse
import asyncio
import json
from pathlib import Path
from typing import Any

import mcp.server.stdio
from mcp import types
from mcp.server import NotificationOptions, Server
from mcp.server.models import InitializationOptions

from . import __version__
from .bridge_rpc import (
    BridgeRpcError,
    BridgeRpcInvokeError,
    close_all_sessions,
    rpc_capabilities,
    rpc_health,
    rpc_invoke,
)
from .manifest import ManifestError, ToolManifest, load_manifest
from .project_registry import (
    RuntimeProject,
    discover_runtime_projects,
    find_online_project,
    infer_attached_project_root,
)
from .transforms import TransformError, apply_args_transform, apply_result_transform


def make_server(
    manifest: ToolManifest,
    *,
    explicit_project_root: Path | None = None,
    session_cwd: Path | None = None,
) -> Server:
    server = Server("loomle-python")
    state = SessionState(explicit_project_root=explicit_project_root, session_cwd=session_cwd)

    @server.list_tools()
    async def list_tools() -> list[types.Tool]:
        tools = []
        for tool in manifest.tools_for("python"):
            declared_tool = {
                "name": tool["name"],
                "description": tool["description"],
                "inputSchema": tool["inputSchema"],
            }
            tools.append(types.Tool(**declared_tool))
        return tools

    @server.call_tool()
    async def call_tool(name: str, arguments: dict[str, Any]) -> types.CallToolResult:
        if name == "status":
            state.try_auto_attach()
            projects = discover_runtime_projects("all")
            online_project_count = len(
                [project for project in projects if project.status == "online"]
            )
            issues: list[dict[str, Any]] = []
            runtime: dict[str, Any] = {
                "state": "no_project",
                "rpcConnected": False,
                "listenerReady": False,
                "isPIE": False,
                "editorBusyReason": "NO_PROJECT_ATTACHED",
                "health": None,
                "capabilities": None,
            }
            if state.attached_project is None:
                issues.append(
                    {
                        "code": "NO_PROJECT_ATTACHED",
                        "severity": "error",
                        "domain": "project",
                        "message": "No Unreal project is attached.",
                    }
                )
            else:
                try:
                    health = await rpc_health(state.attached_project.endpoint)
                    try:
                        capabilities = await rpc_capabilities(state.attached_project.endpoint)
                    except (BridgeRpcError, OSError):
                        capabilities = None
                    health_status = str(health.get("status") or "ready")
                    if health_status in ("ok", "ready"):
                        runtime_state = "ready"
                    elif health_status == "degraded":
                        runtime_state = "degraded"
                    else:
                        runtime_state = "error"
                    runtime = {
                        "state": runtime_state,
                        "rpcConnected": True,
                        "listenerReady": True,
                        "isPIE": bool(health.get("isPIE", False)),
                        "editorBusyReason": str(health.get("editorBusyReason", "")),
                        "health": health,
                        "capabilities": capabilities,
                    }
                    if runtime_state != "ready":
                        issues.append(
                            {
                                "code": "RUNTIME_NOT_READY",
                                "severity": "warning",
                                "domain": "runtime",
                                "message": "The LOOMLE runtime is connected but not ready.",
                            }
                        )
                except BridgeRpcInvokeError as exc:
                    runtime = {
                        "state": "error",
                        "rpcConnected": True,
                        "listenerReady": False,
                        "isPIE": False,
                        "editorBusyReason": "RUNTIME_ERROR",
                        "health": exc.payload,
                        "capabilities": None,
                    }
                    issues.append(
                        {
                            "code": str(exc.code),
                            "severity": "error",
                            "domain": "runtime",
                            "message": exc.message,
                        }
                    )
                except (BridgeRpcError, OSError) as exc:
                    runtime = {
                        "state": "unavailable",
                        "rpcConnected": False,
                        "listenerReady": False,
                        "isPIE": False,
                        "editorBusyReason": "RUNTIME_UNAVAILABLE",
                        "health": None,
                        "capabilities": None,
                    }
                    issues.append(
                        {
                            "code": "RUNTIME_UNAVAILABLE",
                            "severity": "error",
                            "domain": "runtime",
                            "message": str(exc),
                        }
                    )

            if runtime["state"] == "ready":
                status = "ready"
                message = "Loomle runtime is ready."
            elif runtime["state"] in ("no_project", "unavailable"):
                status = "offline"
                message = "Loomle runtime is offline."
            else:
                status = "degraded"
                message = "Loomle runtime is degraded."
            return structured_result(
                {
                    "schemaVersion": 1,
                    "status": status,
                    "message": message,
                    "mcp": {
                        "server": "python",
                        "version": __version__,
                    },
                    "project": {
                        "attached": state.attached_project is not None,
                        "root": (
                            str(state.attached_project.project_root)
                            if state.attached_project is not None
                            else None
                        ),
                        "endpoint": (
                            str(state.attached_project.endpoint)
                            if state.attached_project is not None
                            else None
                        ),
                        "discovered": {
                            "online": online_project_count,
                            "total": len(projects),
                        },
                    },
                    "runtime": runtime,
                    "update": None,
                    "observability": None,
                    "issues": issues,
                }
            )

        if name == "project.list":
            try:
                status = str(arguments.get("status") or "online")
                include_diagnostics = bool(arguments.get("includeDiagnostics") or False)
                projects = discover_runtime_projects(status)  # type: ignore[arg-type]
            except ValueError as exc:
                return error_result(
                    {
                        "isError": True,
                        "code": "INVALID_ARGUMENT",
                        "message": str(exc),
                        "retryable": False,
                    }
                )
            return structured_result(
                {
                    "projects": [
                        project.to_json(include_diagnostics=include_diagnostics)
                        for project in projects
                    ]
                }
            )

        if name == "project.attach":
            project_id = arguments.get("projectId")
            project_root = arguments.get("projectRoot")
            if not isinstance(project_id, str):
                project_id = None
            if not isinstance(project_root, str):
                project_root = None
            if project_id is None and project_root is None:
                return error_result(
                    {
                        "isError": True,
                        "code": "INVALID_ARGUMENT",
                        "message": "project.attach requires projectId or projectRoot.",
                        "retryable": False,
                    }
                )

            project = find_online_project(project_id=project_id, project_root=project_root)
            if project is None:
                return error_result(
                    {
                        "isError": True,
                        "code": "PROJECT_NOT_FOUND",
                        "message": "No online project matched projectId/projectRoot. Use project.list with status=online.",
                        "retryable": False,
                    }
                )
            if not project.attachable:
                return error_result(
                    {
                        "isError": True,
                        "code": "PROJECT_NOT_ATTACHABLE",
                        "message": f"Project is not attachable: {project.reason or 'unknown reason'}",
                        "retryable": False,
                    }
                )

            state.attached_project = project
            return structured_result(
                {
                    "attached": True,
                    "projectId": project.project_id,
                    "name": project.name,
                    "projectRoot": str(project.project_root),
                    "endpoint": str(project.endpoint),
                }
            )

        if name == "schema.inspect":
            return structured_result(
                manifest.inspect_schema(
                    domain=str(arguments.get("domain", "")),
                    tool_name=str(arguments.get("tool", "")),
                    operation=arguments.get("operation"),
                    include=parse_include(arguments.get("include")),
                )
            )

        tool = manifest.get_tool(name)
        if tool is not None and tool.get("dispatch", {}).get("kind") == "bridgeRpc":
            return await call_bridge_rpc_tool(state, tool, arguments)

        return error_result(
            {
                "isError": True,
                "code": "NOT_IMPLEMENTED",
                "message": (
                    f"Python MCP tool dispatch is not implemented yet for {name}. "
                    "This slice currently validates the official MCP SDK server shell, tools/list, and schema.inspect."
                ),
                "retryable": False,
            }
        )

    return server


async def call_bridge_rpc_tool(
    state: "SessionState",
    tool: dict[str, Any],
    arguments: dict[str, Any],
) -> types.CallToolResult:
    state.try_auto_attach()
    if state.attached_project is None:
        return error_result(
            {
                "isError": True,
                "code": "NO_PROJECT_ATTACHED",
                "message": "No project attached. Use project.list with status=online, then project.attach.",
                "retryable": True,
            }
        )

    dispatch = tool.get("dispatch")
    if not isinstance(dispatch, dict):
        return error_result(
            {
                "isError": True,
                "code": "INVALID_TOOL_MANIFEST",
                "message": f"Tool {tool.get('name')} is missing dispatch configuration.",
                "retryable": False,
            }
        )
    try:
        bridge_arguments = apply_args_transform(dispatch.get("args"), arguments)
    except TransformError as exc:
        return error_result(
            {
                "isError": True,
                "code": "INVALID_ARGUMENT",
                "message": str(exc),
                "retryable": False,
            }
        )

    bridge_tool = dispatch.get("tool")
    if isinstance(bridge_arguments.get("__bridgeTool"), str):
        bridge_tool = bridge_arguments.pop("__bridgeTool")
    if not isinstance(bridge_tool, str) or not bridge_tool:
        return error_result(
            {
                "isError": True,
                "code": "INVALID_TOOL_MANIFEST",
                "message": f"Tool {tool.get('name')} bridgeRpc dispatch requires a target tool.",
                "retryable": False,
            }
        )

    try:
        payload = await rpc_invoke(state.attached_project.endpoint, bridge_tool, bridge_arguments)
        try:
            payload = apply_result_transform(dispatch.get("result"), payload, arguments)
        except TransformError as exc:
            return error_result(
                {
                    "isError": True,
                    "code": "TRANSFORM_NOT_IMPLEMENTED",
                    "message": str(exc),
                    "retryable": False,
                }
            )
        if payload.get("isError") is True:
            return error_result(payload)
        return structured_result(payload)
    except BridgeRpcInvokeError as exc:
        return error_result(
            {
                "isError": True,
                "code": exc.code,
                "message": exc.message,
                "retryable": exc.retryable,
                "detail": exc.detail,
            }
        )
    except BridgeRpcError as exc:
        return error_result(
            {
                "isError": True,
                "code": "BRIDGE_UNAVAILABLE",
                "message": str(exc),
                "retryable": True,
            }
        )


class SessionState:
    def __init__(self, *, explicit_project_root: Path | None, session_cwd: Path | None) -> None:
        self.attached_project: RuntimeProject | None = None
        self.explicit_project_root = explicit_project_root
        self.session_cwd = session_cwd

    def try_auto_attach(self) -> None:
        if self.attached_project is not None:
            return
        online_projects = discover_runtime_projects("online")
        project_root = infer_attached_project_root(
            explicit_project_root=self.explicit_project_root,
            cwd=self.session_cwd,
            online_projects=online_projects,
        )
        if project_root is None:
            return
        for project in online_projects:
            if project.project_root == project_root:
                self.attached_project = project
                return


def parse_include(value: Any) -> list[str] | None:
    if value is None:
        return None
    if not isinstance(value, list):
        raise ManifestError("schema.inspect include must be an array.")
    includes: list[str] = []
    for item in value:
        if not isinstance(item, str):
            raise ManifestError("schema.inspect include entries must be strings.")
        includes.append(item)
    return includes


def structured_result(payload: dict[str, Any]) -> types.CallToolResult:
    return types.CallToolResult(
        content=[types.TextContent(type="text", text=json.dumps(payload, ensure_ascii=False))],
        structuredContent=payload,
    )


def error_result(payload: dict[str, Any]) -> types.CallToolResult:
    return types.CallToolResult(
        content=[types.TextContent(type="text", text=str(payload.get("message", "Tool error")))],
        structuredContent=payload,
        isError=True,
    )


async def run_stdio(
    manifest_path: str | None = None,
    *,
    explicit_project_root: Path | None = None,
    session_cwd: Path | None = None,
) -> None:
    manifest = load_manifest(manifest_path)
    server = make_server(
        manifest,
        explicit_project_root=explicit_project_root,
        session_cwd=session_cwd,
    )
    async with mcp.server.stdio.stdio_server() as (read_stream, write_stream):
        try:
            await server.run(
                read_stream,
                write_stream,
                InitializationOptions(
                    server_name="loomle-python",
                    server_version=__version__,
                    capabilities=server.get_capabilities(
                        notification_options=NotificationOptions(),
                        experimental_capabilities={},
                    ),
                ),
            )
        finally:
            await close_all_sessions()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="LOOMLE Python MCP server.")
    parser.add_argument("--manifest", help="Path to mcp/manifest/manifest.json.")
    parser.add_argument(
        "--project-root",
        help="Optional UE project root attach hint. project.attach remains the canonical session selection path.",
    )
    args = parser.parse_args(argv)

    try:
        project_root = Path(args.project_root).resolve() if args.project_root else None
        asyncio.run(
            run_stdio(
                args.manifest,
                explicit_project_root=project_root,
                session_cwd=Path.cwd(),
            )
        )
    except ManifestError as exc:
        print(f"[loomle-python-mcp][ERROR] {exc}")
        return 1
    return 0
