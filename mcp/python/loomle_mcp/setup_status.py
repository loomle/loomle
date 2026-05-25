from __future__ import annotations

import json
import os
import shutil
import time
from pathlib import Path
from typing import Any

from .project_registry import RuntimeProject, discover_runtime_projects, home_dir, loomle_root


def build_setup_status(attached_project: RuntimeProject | None = None) -> dict[str, Any]:
    projects = discover_runtime_projects("all")
    project = attached_project or select_single_online_project(projects)
    hosts = detect_mcp_hosts(home_dir())
    native_configured = any(
        host["loomleEntry"]["owner"] == "native"
        for host in hosts
    )
    fab_python = detect_fab_python_mcp(project)
    bridge_state = bridge_state_for_project(project)
    return {
        "schemaVersion": 1,
        "channel": channel_for_project(project),
        "bridge": bridge_status(project, bridge_state),
        "plugin": plugin_status(project),
        "nativeCli": native_cli_status(hosts),
        "fabPythonMcp": fab_python,
        "hosts": hosts,
        "recommendation": recommendation(
            bridge_state=bridge_state,
            native_configured=native_configured,
            fab_python_available=bool(fab_python["available"]),
            hosts=hosts,
        ),
    }


class SetupConfigureError(RuntimeError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.message = message

    def to_payload(self) -> dict[str, Any]:
        return {
            "isError": True,
            "code": self.code,
            "message": self.message,
            "retryable": False,
        }


def configure_setup(
    *,
    host: str,
    server: str = "auto",
    attached_project: RuntimeProject | None = None,
) -> dict[str, Any]:
    if host not in {"codex", "claude"}:
        raise SetupConfigureError("INVALID_ARGUMENT", "host must be codex or claude.")
    if server not in {"auto", "fabPython", "native"}:
        raise SetupConfigureError(
            "INVALID_ARGUMENT",
            "server must be auto, fabPython, or native.",
        )

    project = attached_project or select_single_online_project(discover_runtime_projects("all"))
    hosts = detect_mcp_hosts(home_dir())
    if any(item["loomleEntry"]["owner"] == "native" for item in hosts):
        raise SetupConfigureError(
            "NATIVE_CONFIGURED",
            "Native loomle mcp is already configured; keeping native and not writing Fab Python MCP config.",
        )
    host_status = next((item for item in hosts if item["id"] == host), None)
    if host_status is None:
        raise SetupConfigureError("HOST_CONFIG_UNAVAILABLE", "Unknown MCP host.")
    if not host_status["canAutoConfigure"]:
        code = "LOOMLE_ENTRY_EXISTS" if host_status["loomleEntry"]["present"] else "CONFIGURATION_BLOCKED"
        raise SetupConfigureError(
            code,
            f"setup.configure cannot safely configure {host}: {host_status.get('reason') or 'host config is not safe to edit'}",
        )

    fab_python = detect_fab_python_mcp(project)
    native_cli = native_cli_status(hosts)
    selected_server = select_server(server, fab_python, native_cli)
    if selected_server == "fab":
        config = fab_python.get("config")
        if not isinstance(config, dict):
            raise SetupConfigureError("FAB_PYTHON_UNAVAILABLE", "Fab Python MCP files are missing.")
    else:
        command = native_cli.get("path")
        if not isinstance(command, str) or not command:
            raise SetupConfigureError("NATIVE_CLI_UNAVAILABLE", "Native loomle CLI path is missing.")
        config = {"command": command, "args": ["mcp"]}

    config_path = Path(str(host_status["configPath"]))
    if host == "codex":
        return write_codex_mcp_config(config_path, config, selected_server)
    if selected_server == "fab":
        return write_claude_desktop_mcp_config(config_path, config, selected_server)
    raise SetupConfigureError(
        "MANUAL_CONFIG_REQUIRED",
        "Claude native setup should use `claude mcp add --scope user loomle -- <loomle> mcp` outside MCP.",
    )


def select_server(server: str, fab_python: dict[str, Any], native_cli: dict[str, Any]) -> str:
    if server == "fabPython":
        return "fab"
    if server == "native":
        return "native"
    if fab_python.get("available"):
        return "fab"
    if native_cli.get("detected"):
        return "native"
    raise SetupConfigureError(
        "MANUAL_CONFIG_REQUIRED",
        "Neither Fab Python MCP nor native loomle CLI is available for automatic configuration.",
    )


def write_codex_mcp_config(
    config_path: Path,
    config: dict[str, Any],
    selected_server: str,
) -> dict[str, Any]:
    raw = read_text(config_path) or ""
    if classify_loomle_mcp_entry(raw)[0]:
        raise SetupConfigureError("LOOMLE_ENTRY_EXISTS", "Codex already has a loomle MCP entry.")
    backup_path = backup_existing_config(config_path)
    command = config.get("command")
    args = config.get("args")
    if not isinstance(command, str) or not isinstance(args, list):
        raise SetupConfigureError("CONFIG_WRITE_FAILED", "Invalid MCP config.")
    args_text = ", ".join(toml_string(str(arg)) for arg in args)
    next_text = raw
    if next_text and not next_text.endswith("\n"):
        next_text += "\n"
    next_text += "\n[mcp_servers.loomle]\n"
    next_text += f"command = {toml_string(command)}\n"
    next_text += f"args = [{args_text}]\n"
    try:
        config_path.write_text(next_text, encoding="utf-8")
    except OSError as exc:
        raise SetupConfigureError(
            "CONFIG_WRITE_FAILED",
            f"Failed to write {config_path}: {exc}",
        ) from exc
    return setup_configure_success("codex", selected_server, config_path, backup_path)


def write_claude_desktop_mcp_config(
    config_path: Path,
    config: dict[str, Any],
    selected_server: str,
) -> dict[str, Any]:
    raw = read_text(config_path) or ""
    if classify_loomle_mcp_entry(raw)[0]:
        raise SetupConfigureError("LOOMLE_ENTRY_EXISTS", "Claude already has a loomle MCP entry.")
    try:
        root = json.loads(raw) if raw.strip() else {}
    except json.JSONDecodeError as exc:
        raise SetupConfigureError(
            "CONFIG_WRITE_FAILED",
            f"Failed to parse {config_path}: {exc}",
        ) from exc
    if not isinstance(root, dict):
        raise SetupConfigureError("CONFIG_WRITE_FAILED", "Claude config root must be an object.")
    backup_path = backup_existing_config(config_path)
    mcp_servers = root.setdefault("mcpServers", {})
    if not isinstance(mcp_servers, dict):
        raise SetupConfigureError("CONFIG_WRITE_FAILED", "Claude config mcpServers must be an object.")
    mcp_servers["loomle"] = config
    try:
        config_path.write_text(json.dumps(root, indent=2) + "\n", encoding="utf-8")
    except OSError as exc:
        raise SetupConfigureError(
            "CONFIG_WRITE_FAILED",
            f"Failed to write {config_path}: {exc}",
        ) from exc
    return setup_configure_success("claude", selected_server, config_path, backup_path)


def backup_existing_config(config_path: Path) -> Path | None:
    if not config_path.exists():
        return None
    backup_path = config_path.with_name(
        f"{config_path.name}.loomle-backup-{int(time.time())}"
    )
    try:
        shutil.copy2(config_path, backup_path)
    except OSError as exc:
        raise SetupConfigureError(
            "BACKUP_FAILED",
            f"Failed to back up {config_path}: {exc}",
        ) from exc
    return backup_path


def setup_configure_success(
    host: str,
    selected_server: str,
    config_path: Path,
    backup_path: Path | None,
) -> dict[str, Any]:
    server_owner = "fab" if selected_server == "fab" else "native"
    return {
        "configured": True,
        "host": host,
        "serverOwner": server_owner,
        "configPath": str(config_path),
        "backupPath": str(backup_path) if backup_path is not None else None,
        "changed": True,
        "message": f"Configured {host} to use LOOMLE {server_owner} MCP.",
    }


def toml_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def select_single_online_project(projects: list[RuntimeProject]) -> RuntimeProject | None:
    online = [project for project in projects if project.status == "online"]
    return online[0] if len(online) == 1 else None


def bridge_state_for_project(project: RuntimeProject | None) -> str:
    if project is None:
        return "offline"
    if project.status == "online" and project.attachable:
        return "ready"
    if project.status == "online":
        return "degraded"
    return "offline"


def channel_for_project(project: RuntimeProject | None) -> str:
    if project is None:
        return "unknown"
    if project.plugin_managed_by == "native":
        return "native"
    if project.plugin_managed_by == "fab":
        return "fab"
    return "unknown"


def bridge_status(project: RuntimeProject | None, state: str) -> dict[str, Any]:
    return {
        "state": state,
        "endpoint": str(project.endpoint) if project is not None else None,
        "runtimeRegistered": project is not None and project.status == "online",
        "projectRegistered": project is not None,
        "projectId": project.project_id if project is not None else None,
        "projectRoot": str(project.project_root) if project is not None else None,
        "uproject": str(project.uproject) if project is not None and project.uproject is not None else None,
    }


def plugin_status(project: RuntimeProject | None) -> dict[str, Any]:
    plugin_path = project.plugin_path if project is not None else None
    return {
        "path": str(plugin_path) if plugin_path is not None else None,
        "version": project.plugin_version if project is not None else None,
        "installScope": project.plugin_install_scope if project is not None and project.plugin_install_scope else "unknown",
        "managedBy": project.plugin_managed_by if project is not None and project.plugin_managed_by else "unknown",
        "hasFabPythonMcp": fab_python_mcp_server_path(plugin_path).is_file() if plugin_path is not None else False,
    }


def detect_fab_python_mcp(project: RuntimeProject | None) -> dict[str, Any]:
    server_path = (
        fab_python_mcp_server_path(project.plugin_path)
        if project is not None and project.plugin_path is not None
        else None
    )
    available = server_path is not None and server_path.is_file()
    mcp_dir = str(server_path.parent) if server_path is not None else None
    return {
        "available": available,
        "path": str(server_path) if server_path is not None else None,
        "recommendedCommand": "uv" if available else None,
        "config": {
            "command": "uv",
            "args": ["--directory", mcp_dir, "run", "loomle_mcp_server.py"],
        } if available and mcp_dir is not None else None,
    }


def fab_python_mcp_server_path(plugin_path: Path) -> Path:
    return plugin_path / "Resources" / "MCP" / "loomle_mcp_server.py"


def detect_mcp_hosts(home: Path) -> list[dict[str, Any]]:
    return [
        detect_mcp_host("codex", codex_config_path(home)),
        detect_mcp_host("claude", claude_config_path(home)),
    ]


def codex_config_path(home: Path) -> Path:
    return home / ".codex" / "config.toml"


def claude_config_path(home: Path) -> Path:
    if os.name == "nt":
        appdata = os.environ.get("APPDATA")
        if appdata:
            return Path(appdata) / "Claude" / "claude_desktop_config.json"
    if sys_platform() == "darwin":
        return home / "Library" / "Application Support" / "Claude" / "claude_desktop_config.json"
    return home / ".config" / "Claude" / "claude_desktop_config.json"


def sys_platform() -> str:
    import sys
    return sys.platform


def detect_mcp_host(host_id: str, config_path: Path) -> dict[str, Any]:
    raw = read_text(config_path)
    entry_present, owner, server_name = classify_loomle_mcp_entry(raw) if raw is not None else (False, None, None)
    parent_exists = config_path.parent.is_dir()
    if owner == "native":
        reason = "nativeConfigured"
    elif entry_present:
        reason = "loomleEntryExists"
    elif not parent_exists:
        reason = "configDirectoryMissing"
    else:
        reason = None
    return {
        "id": host_id,
        "detected": config_path.is_file() or parent_exists,
        "configPath": str(config_path),
        "loomleEntry": {
            "present": entry_present,
            "owner": owner,
            "serverName": server_name,
        },
        "canAutoConfigure": parent_exists and not entry_present,
        "reason": reason,
    }


def read_text(path: Path) -> str | None:
    try:
        return path.read_text(encoding="utf-8")
    except OSError:
        return None


def classify_loomle_mcp_entry(raw: str) -> tuple[bool, str | None, str | None]:
    lower = raw.lower()
    entry_present = (
        "[mcp_servers.loomle]" in lower
        or "[mcp.servers.loomle]" in lower
        or ("mcpservers" in lower and '"loomle"' in lower)
    )
    if not entry_present:
        return False, None, None
    if "loomle_mcp_server.py" in lower or "resources/mcp" in lower:
        owner = "fab"
    elif (
        ".loomle/bin/loomle" in lower
        or "\\.loomle\\bin\\loomle" in lower
        or "loomle mcp" in lower
    ):
        owner = "native"
    else:
        owner = "manual"
    return True, owner, "loomle"


def native_cli_status(hosts: list[dict[str, Any]]) -> dict[str, Any]:
    configured_hosts = [
        host["id"]
        for host in hosts
        if host["loomleEntry"]["owner"] == "native"
    ]
    cli_path = loomle_root() / "bin" / ("loomle.exe" if os.name == "nt" else "loomle")
    version = read_active_version()
    return {
        "detected": cli_path.is_file() or version is not None or bool(configured_hosts),
        "path": str(cli_path) if cli_path.exists() else None,
        "version": version,
        "configuredHosts": configured_hosts,
    }


def read_active_version() -> str | None:
    active_path = loomle_root() / "install" / "active.json"
    try:
        value = json.loads(active_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    if not isinstance(value, dict):
        return None
    version = value.get("activeVersion") or value.get("installedVersion")
    return str(version) if version else None


def recommendation(
    *,
    bridge_state: str,
    native_configured: bool,
    fab_python_available: bool,
    hosts: list[dict[str, Any]],
) -> dict[str, Any]:
    if bridge_state != "ready":
        return {
            "action": "fixBridge",
            "message": "LoomleBridge is not ready. Open the Unreal project and make sure the plugin is enabled.",
            "safeAutomaticActions": [],
            "warnings": [],
        }
    if native_configured:
        return {
            "action": "keepNative",
            "message": "Native loomle is already configured and can connect to this project.",
            "safeAutomaticActions": [],
            "warnings": [],
        }
    if fab_python_available:
        safe_actions = [
            f"configure{host['id'].capitalize()}"
            for host in hosts
            if host["canAutoConfigure"]
        ]
        if safe_actions:
            return {
                "action": "configureFabPython",
                "message": "Fab Python MCP is available. A detected MCP host can be configured through setup.configure or the Unreal setup panel.",
                "safeAutomaticActions": safe_actions,
                "warnings": [],
            }
        return {
            "action": "showManualConfig",
            "message": "Fab Python MCP is available, but no MCP host config path was detected unambiguously.",
            "safeAutomaticActions": [],
            "warnings": [],
        }
    return {
        "action": "noAction",
        "message": "No setup action is currently available.",
        "safeAutomaticActions": [],
        "warnings": [],
    }
