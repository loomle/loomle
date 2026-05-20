from __future__ import annotations

import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Literal

ProjectStatus = Literal["online", "offline", "all"]


@dataclass(frozen=True)
class RuntimeProject:
    project_id: str
    name: str
    project_root: Path
    uproject: Path | None
    endpoint: Path
    status: str
    attachable: bool
    plugin_installed: bool
    plugin_version: str | None
    protocol_version: int | None
    last_seen_at: str | None
    reason: str | None

    def to_json(self, *, include_diagnostics: bool = False) -> dict[str, Any]:
        value: dict[str, Any] = {
            "projectId": self.project_id,
            "name": self.name,
            "projectRoot": str(self.project_root),
            "uproject": str(self.uproject) if self.uproject is not None else None,
            "status": self.status,
            "attachable": self.attachable,
            "pluginInstalled": self.plugin_installed,
            "pluginVersion": self.plugin_version,
            "protocolVersion": self.protocol_version,
            "lastSeenAt": self.last_seen_at,
            "reason": self.reason,
        }
        if include_diagnostics:
            value["diagnostics"] = {
                "endpoint": str(self.endpoint),
                "endpointExists": self.endpoint.exists(),
            }
        return value


def home_dir() -> Path:
    if os.name == "nt":
        return Path(os.environ.get("USERPROFILE", str(Path.home())))
    return Path(os.environ.get("HOME", str(Path.home())))


def loomle_root() -> Path:
    override = os.environ.get("LOOMLE_HOME")
    if override:
        return Path(override)
    return home_dir() / ".loomle"


def runtime_registry_dir() -> Path:
    return loomle_root() / "state" / "runtimes"


def project_registry_dir() -> Path:
    return loomle_root() / "state" / "projects"


def discover_runtime_projects(status: ProjectStatus = "online") -> list[RuntimeProject]:
    if status not in {"online", "offline", "all"}:
        raise ValueError(f"invalid project.list status: {status}")

    runtimes = read_runtime_records()
    projects: list[RuntimeProject] = []

    for record in read_project_records():
        project_id = project_record_project_id(record)
        runtime = runtimes.pop(project_id, None)
        project = project_record_to_project(record, runtime)
        if include_project(project, status):
            projects.append(project)

    for runtime in runtimes.values():
        project = runtime_record_to_project(runtime)
        if include_project(project, status):
            projects.append(project)

    projects.sort(key=lambda project: project.name)
    return projects


def include_project(project: RuntimeProject, status: ProjectStatus) -> bool:
    if status == "all":
        return True
    return project.status == status


def infer_attached_project_root(
    *,
    explicit_project_root: Path | None,
    cwd: Path | None,
    online_projects: list[RuntimeProject],
) -> Path | None:
    if explicit_project_root is not None:
        return explicit_project_root

    if cwd is not None:
        try:
            resolved_cwd = cwd.resolve()
        except OSError:
            resolved_cwd = cwd
        matching = [
            project
            for project in online_projects
            if is_relative_to(resolved_cwd, project.project_root)
        ]
        if matching:
            return max(
                matching,
                key=lambda project: len(project.project_root.parts),
            ).project_root

    if len(online_projects) == 1:
        return online_projects[0].project_root

    return None


def find_online_project(
    *,
    project_id: str | None = None,
    project_root: str | None = None,
) -> RuntimeProject | None:
    projects = discover_runtime_projects("online")
    for project in projects:
        if project_id is not None and project.project_id == project_id:
            return project
        if project_root is not None and str(project.project_root) == project_root:
            return project
    if project_root is not None and project_id is None:
        return project_root_online_project(Path(project_root))
    return None


def project_root_online_project(project_root: Path) -> RuntimeProject | None:
    try:
        resolved_root = project_root.resolve()
    except OSError:
        resolved_root = project_root
    if not resolved_root.is_dir():
        return None
    endpoint = runtime_endpoint_path_for_project_root(resolved_root)
    if not endpoint.exists():
        return None
    uproject = find_project_uproject(resolved_root)
    return RuntimeProject(
        project_id=stable_project_id(resolved_root),
        name=resolved_root.name or "Unreal Project",
        project_root=resolved_root,
        uproject=uproject,
        endpoint=endpoint,
        status="online",
        attachable=True,
        plugin_installed=(resolved_root / "Plugins" / "LoomleBridge").is_dir(),
        plugin_version=None,
        protocol_version=None,
        last_seen_at=None,
        reason=None,
    )


def read_runtime_records() -> dict[str, dict[str, Any]]:
    records: dict[str, dict[str, Any]] = {}
    for record in read_json_records(runtime_registry_dir()):
        records[runtime_record_project_id(record)] = record
    return records


def read_project_records() -> list[dict[str, Any]]:
    return read_json_records(project_registry_dir())


def read_json_records(directory: Path) -> list[dict[str, Any]]:
    if not directory.is_dir():
        return []
    records: list[dict[str, Any]] = []
    for path in sorted(directory.iterdir()):
        if path.suffix != ".json":
            continue
        try:
            value = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        if isinstance(value, dict):
            records.append(value)
    return records


def runtime_record_project_id(record: dict[str, Any]) -> str:
    return str(
        record.get("projectId")
        or record.get("runtimeId")
        or stable_project_id(path_from_record(record, "projectRoot"))
    )


def project_record_project_id(record: dict[str, Any]) -> str:
    return str(record.get("projectId") or stable_project_id(path_from_record(record, "projectRoot")))


def project_record_to_project(
    record: dict[str, Any],
    runtime: dict[str, Any] | None,
) -> RuntimeProject:
    project_root = path_from_record(record, "projectRoot")
    endpoint = (
        path_from_record(runtime, "endpoint")
        if runtime is not None and runtime.get("endpoint")
        else runtime_endpoint_path_for_project_root(project_root)
    )
    endpoint_exists = runtime is not None and endpoint.exists()
    status = "online" if endpoint_exists else "offline"
    project_id = str(record.get("projectId") or stable_project_id(project_root))
    name = str(
        (runtime or {}).get("name")
        or record.get("name")
        or project_root.name
        or "Unreal Project"
    )

    return RuntimeProject(
        project_id=project_id,
        name=name,
        project_root=project_root,
        uproject=optional_path((runtime or {}).get("uproject") or record.get("uproject")),
        endpoint=endpoint,
        status=status,
        attachable=endpoint_exists,
        plugin_installed=(project_root / "Plugins" / "LoomleBridge").is_dir(),
        plugin_version=(runtime or {}).get("pluginVersion") or record.get("pluginVersion"),
        protocol_version=optional_int((runtime or {}).get("protocolVersion")),
        last_seen_at=(runtime or {}).get("lastSeenAt") or record.get("lastSeenAt"),
        reason=None if endpoint_exists else "LOOMLE runtime endpoint is not available",
    )


def runtime_record_to_project(record: dict[str, Any]) -> RuntimeProject:
    project_root = path_from_record(record, "projectRoot")
    endpoint = (
        path_from_record(record, "endpoint")
        if record.get("endpoint")
        else runtime_endpoint_path_for_project_root(project_root)
    )
    endpoint_exists = endpoint.exists()
    status = "online" if endpoint_exists else "offline"

    return RuntimeProject(
        project_id=runtime_record_project_id(record),
        name=str(record.get("name") or project_root.name or "Unreal Project"),
        project_root=project_root,
        uproject=optional_path(record.get("uproject")),
        endpoint=endpoint,
        status=status,
        attachable=endpoint_exists,
        plugin_installed=True,
        plugin_version=record.get("pluginVersion"),
        protocol_version=optional_int(record.get("protocolVersion")),
        last_seen_at=record.get("lastSeenAt"),
        reason=None if endpoint_exists else "LOOMLE runtime endpoint is not available",
    )


def runtime_endpoint_path_for_project_root(project_root: Path) -> Path:
    if os.name == "nt":
        return Path(rf"\\.\pipe\{runtime_pipe_name_for_project_root(project_root)}")
    return project_root / "Intermediate" / "loomle.sock"


def runtime_pipe_name_for_project_root(project_root: Path) -> str:
    return f"loomle-{stable_project_id(project_root)}"


def stable_project_id(project_root: Path) -> str:
    normalized = str(project_root).replace("\\", "/").rstrip("/").lower()
    return f"{stable_fnv1a64(normalized.encode('utf-8')):016x}"


def stable_fnv1a64(data: bytes) -> int:
    value = 0xCBF29CE484222325
    for byte in data:
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def path_from_record(record: dict[str, Any] | None, key: str) -> Path:
    if record is None:
        return Path()
    value = record.get(key)
    return Path(str(value)) if value else Path()


def optional_path(value: Any) -> Path | None:
    return Path(str(value)) if value else None


def optional_int(value: Any) -> int | None:
    if value is None:
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def find_project_uproject(project_root: Path) -> Path | None:
    try:
        for path in project_root.iterdir():
            if path.is_file() and path.suffix.lower() == ".uproject":
                return path
    except OSError:
        return None
    return None


def is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False
