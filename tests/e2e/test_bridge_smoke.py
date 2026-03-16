#!/usr/bin/env python3
import argparse
import json
import queue
import subprocess
import sys
import threading
import time
from collections import deque
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS_DIR = REPO_ROOT / "tools"

REQUIRED_TOOLS = {
    "loomle",
    "graph",
    "graph.list",
    "graph.resolve",
    "graph.query",
    "graph.actions",
    "graph.mutate",
    "context",
    "editor.open",
    "editor.focus",
    "editor.screenshot",
    "execute",
}

EXPECTED_GRAPH_MUTATE_OPS = {
    "addNode.byClass",
    "addNode.byAction",
    "connectPins",
    "disconnectPins",
    "breakPinLinks",
    "setPinDefault",
    "removeNode",
    "moveNode",
    "moveNodeBy",
    "moveNodes",
    "layoutGraph",
    "compile",
    "runScript",
}


def fail(msg: str) -> None:
    print(f"[FAIL] {msg}")
    raise SystemExit(1)


def _compact_json(value: Any, limit: int = 2000) -> str:
    text = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    if len(text) <= limit:
        return text
    return text[: limit - 3] + "..."


def is_tool_error_payload(payload: dict[str, Any]) -> bool:
    if bool(payload.get("isError")):
        return True
    domain_code = payload.get("domainCode")
    if isinstance(domain_code, str) and domain_code.strip():
        return True
    message = payload.get("message")
    if isinstance(message, str) and message.strip():
        return True
    return False


class McpStdioClient:
    def __init__(self, project_root: Path, server_binary: Path, timeout_s: float) -> None:
        if not server_binary.exists():
            fail(f"loomle binary not found: {server_binary}")
        if not server_binary.is_file():
            fail(f"loomle binary path is not a file: {server_binary}")
        if not any(project_root.glob("*.uproject")):
            fail(f"no .uproject found under: {project_root}")

        self.proc = subprocess.Popen(
            [str(server_binary), "--project-root", str(project_root), "session"],
            cwd=str(project_root),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
        self.timeout_s = timeout_s
        self._stdout_queue: queue.Queue[str] = queue.Queue()
        self._reader_error: Exception | None = None
        self._reader_thread = threading.Thread(target=self._stdout_reader, name="loomle-mcp-stdout-reader", daemon=True)
        self._reader_thread.start()
        self._stderr_tail: deque[str] = deque(maxlen=200)
        self._stderr_reader_error: Exception | None = None
        self._stderr_lock = threading.Lock()
        self._stderr_thread = threading.Thread(target=self._stderr_reader, name="loomle-mcp-stderr-reader", daemon=True)
        self._stderr_thread.start()
        self._pending_responses: dict[int, dict[str, Any]] = {}

    def _stdout_reader(self) -> None:
        try:
            if self.proc.stdout is None:
                return
            for line in self.proc.stdout:
                self._stdout_queue.put(line)
        except Exception as exc:
            self._reader_error = exc

    def _stderr_reader(self) -> None:
        try:
            if self.proc.stderr is None:
                return
            for line in self.proc.stderr:
                text = line.rstrip()
                if not text:
                    continue
                with self._stderr_lock:
                    self._stderr_tail.append(text)
        except Exception as exc:
            self._stderr_reader_error = exc

    def _stderr_snapshot(self) -> str:
        with self._stderr_lock:
            if not self._stderr_tail:
                return ""
            return "\n".join(self._stderr_tail)

    def close(self) -> None:
        if self.proc.poll() is None:
            try:
                self.proc.terminate()
                self.proc.wait(timeout=2)
            except Exception:
                self.proc.kill()
        if self._reader_thread.is_alive():
            self._reader_thread.join(timeout=1)
        if self._stderr_thread.is_alive():
            self._stderr_thread.join(timeout=1)

    def request(self, req_id: int, method: str, params: dict[str, Any]) -> dict[str, Any]:
        if self.proc.stdin is None:
            fail("loomle session stdin is not available")

        pending = self._pending_responses.pop(req_id, None)
        if pending is not None:
            if not pending.get("ok", False):
                fail(f"session error for {method}: {pending.get('error')}")
            return pending

        payload = {
            "id": req_id,
            "method": method,
            "params": params,
        }
        self.proc.stdin.write(json.dumps(payload, separators=(",", ":")) + "\n")
        self.proc.stdin.flush()

        deadline = time.time() + self.timeout_s
        while time.time() < deadline:
            if self.proc.poll() is not None:
                err = self._stderr_snapshot()
                fail(f"loomle session exited early: {err}")
            if self._reader_error is not None:
                fail(f"loomle session stdout reader failed: {self._reader_error}")
            if self._stderr_reader_error is not None:
                fail(f"loomle session stderr reader failed: {self._stderr_reader_error}")
            wait_s = max(0.0, deadline - time.time())
            try:
                line = self._stdout_queue.get(timeout=min(0.1, wait_s))
            except queue.Empty:
                continue

            line = line.strip()
            if not line:
                continue

            try:
                frame = json.loads(line)
            except json.JSONDecodeError:
                continue

            frame_id = frame.get("id")
            if frame_id != req_id:
                if isinstance(frame_id, int):
                    self._pending_responses[frame_id] = frame
                    while len(self._pending_responses) > 128:
                        self._pending_responses.pop(next(iter(self._pending_responses)))
                continue

            if not frame.get("ok", False):
                fail(f"session error for {method}: {frame.get('error')}")
            return frame

        stderr_tail = self._stderr_snapshot()
        if stderr_tail:
            fail(f"timeout waiting for {method} id={req_id}; recent stderr:\n{stderr_tail}")
        fail(f"timeout waiting for {method} id={req_id}")


def parse_tool_payload(response: dict[str, Any], method: str) -> dict[str, Any]:
    result = response.get("result")
    if not isinstance(result, dict):
        fail(f"Invalid {method} response: missing result object raw={_compact_json(response)}")

    structured = result.get("structuredContent")
    if isinstance(structured, dict):
        return structured

    content = result.get("content")
    if not isinstance(content, list) or not content:
        fail(f"Invalid {method} response: missing content raw={_compact_json(response)}")

    first = content[0]
    if not isinstance(first, dict):
        fail(f"Invalid {method} response: malformed content item raw={_compact_json(response)}")

    text = first.get("text")
    if not isinstance(text, str):
        fail(f"Invalid {method} response: missing text payload raw={_compact_json(response)}")

    try:
        payload = json.loads(text)
    except json.JSONDecodeError as exc:
        fail(f"Invalid tool payload JSON for {method}: {exc} raw={_compact_json(response)}")

    return payload


def make_temp_asset_path(prefix: str) -> str:
    suffix = time.strftime("%Y%m%d_%H%M%S")
    return f"{prefix}_{suffix}"


def resolve_project_root(project_root_arg: str, dev_config_path_arg: str) -> Path:
    if project_root_arg:
        return Path(project_root_arg).resolve()

    default_path = TOOLS_DIR / "dev.project-root.local.json"
    config_path = Path(dev_config_path_arg).resolve() if dev_config_path_arg else default_path
    if not config_path.exists():
        fail(
            "missing --project-root and dev config not found. "
            f"expected config at {config_path}. copy tools/dev.project-root.example.json "
            "to tools/dev.project-root.local.json and set project_root."
        )

    try:
        raw = json.loads(config_path.read_text(encoding="utf-8"))
    except Exception as exc:
        fail(f"failed to read dev config {config_path}: {exc}")

    value = raw.get("project_root") if isinstance(raw, dict) else None
    if not isinstance(value, str) or not value.strip():
        fail(f"invalid dev config {config_path}: missing string field 'project_root'")
    return Path(value).resolve()


def loomle_binary_name() -> str:
    binary_name = "loomle.exe" if sys.platform.startswith("win") else "loomle"
    return binary_name


def resolve_project_local_loomle_binary(project_root: Path) -> Path:
    return project_root / "Loomle" / loomle_binary_name()


def resolve_repo_loomle_binary() -> Path:
    return REPO_ROOT / "mcp" / "client" / "target" / "release" / loomle_binary_name()


def resolve_default_loomle_binary(project_root: Path) -> Path:
    candidate = resolve_project_local_loomle_binary(project_root)
    if candidate.is_file():
        return candidate
    fail(
        "project-local loomle binary not found: "
        f"{candidate}. install the current checkout into the test project first, "
        "or pass --loomle-bin to override."
    )
    raise RuntimeError("unreachable")


def resolve_default_server_binary(project_root: Path) -> Path:
    return resolve_default_loomle_binary(project_root)


def resolve_default_client_binary(project_root: Path) -> Path:
    return resolve_default_loomle_binary(project_root)


def call_tool(
    client: McpStdioClient,
    req_id: int,
    name: str,
    arguments: dict[str, Any],
    expect_error: bool = False,
) -> dict[str, Any]:
    response = client.request(req_id, "tools/call", {"name": name, "arguments": arguments})
    payload = parse_tool_payload(response, f"tools/call.{name}")
    has_error = is_tool_error_payload(payload)
    if expect_error:
        if not has_error:
            fail(f"expected error for {name}, got payload={_compact_json(payload)} raw={_compact_json(response)}")
        return payload
    if has_error:
        fail(f"{name} failed payload={_compact_json(payload)} raw={_compact_json(response)}")
    return payload


def call_execute_exec_with_retry(
    client: McpStdioClient,
    req_id_base: int,
    code: str,
    max_attempts: int = 20,
    retry_delay_s: float = 1.0,
) -> dict[str, Any]:
    for attempt in range(1, max_attempts + 1):
        req_id = req_id_base + (attempt - 1)
        response = client.request(
            req_id,
            "tools/call",
            {"name": "execute", "arguments": {"mode": "exec", "code": code}},
        )
        payload = parse_tool_payload(response, "tools/call.execute")
        if not is_tool_error_payload(payload):
            return payload

        message = str(payload.get("message", ""))
        detail = str(payload.get("detail", ""))
        if "Python runtime is not initialized" in (message + " " + detail) and attempt < max_attempts:
            print(f"[WARN] execute waiting for Python runtime (attempt {attempt}/{max_attempts})...")
            time.sleep(retry_delay_s)
            continue

        fail(f"execute failed payload={_compact_json(payload)} raw={_compact_json(response)}")

    fail("execute retry loop ended without success")
    raise RuntimeError("unreachable")


def parse_execute_json(payload: dict[str, Any]) -> dict[str, Any]:
    result = payload.get("result")
    candidates: list[str] = []
    if isinstance(result, str) and result.strip():
        candidates.append(result.strip())

    logs = payload.get("logs")
    if isinstance(logs, list):
        for entry in reversed(logs):
            if not isinstance(entry, dict):
                continue
            output = entry.get("output")
            if isinstance(output, str) and output.strip():
                candidates.append(output.strip())

    for candidate in candidates:
        try:
            parsed = json.loads(candidate)
        except json.JSONDecodeError:
            continue
        if isinstance(parsed, dict):
            return parsed

    fail(f"execute payload did not contain a JSON object result: {_compact_json(payload)}")
    raise RuntimeError("unreachable")


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify LOOMLE bridge through loomle session")
    parser.add_argument(
        "--project-root",
        default="",
        help="UE project root, e.g. /Users/xartest/dev/LoomleDevHost. If omitted, read from tools/dev.project-root.local.json",
    )
    parser.add_argument(
        "--dev-config",
        default="",
        help="Optional path to dev project-root config JSON (default: tools/dev.project-root.local.json)",
    )
    parser.add_argument("--timeout", type=float, default=8.0, help="Per-request timeout seconds")
    parser.add_argument(
        "--asset-prefix",
        default="/Game/Codex/BP_BridgeVerify",
        help="Temporary blueprint asset prefix",
    )
    parser.add_argument(
        "--loomle-bin",
        default="",
        help="Override path to the loomle client binary. Defaults to <ProjectRoot>/Loomle/loomle(.exe).",
    )
    parser.add_argument(
        "--mcp-server-bin",
        dest="loomle_bin_compat",
        default="",
        help=argparse.SUPPRESS,
    )
    args = parser.parse_args()

    project_root = resolve_project_root(args.project_root, args.dev_config)
    server_binary = (
        Path(args.loomle_bin).resolve()
        if args.loomle_bin
        else Path(args.loomle_bin_compat).resolve()
        if args.loomle_bin_compat
        else resolve_default_loomle_binary(project_root)
    )

    if not project_root.exists():
        fail(f"project root not found: {project_root}")

    if not any(project_root.glob("*.uproject")):
        fail(f"no .uproject found under: {project_root}")

    client = McpStdioClient(project_root=project_root, server_binary=server_binary, timeout_s=args.timeout)
    temp_asset = make_temp_asset_path(args.asset_prefix)

    try:
        init_resp = client.request(1, "initialize", {})
        protocol_version = init_resp.get("result", {}).get("protocolVersion")
        if not protocol_version:
            fail("initialize did not return protocolVersion")
        print(f"[PASS] initialize protocol={protocol_version}")

        tools_resp = client.request(2, "tools/list", {})
        tools = tools_resp.get("result", {}).get("tools", [])
        tool_names = {
            tool.get("name") for tool in tools if isinstance(tool, dict) and isinstance(tool.get("name"), str)
        }
        missing = sorted(REQUIRED_TOOLS - tool_names)
        if missing:
            fail(f"tools/list missing required tools: {', '.join(missing)}")
        print(f"[PASS] tools/list includes required baseline tools ({len(REQUIRED_TOOLS)})")

        loomle_payload = call_tool(client, 3, "loomle", {})
        if loomle_payload.get("status") not in {"ok", "degraded"}:
            fail(f"loomle unexpected status: {loomle_payload}")
        rpc_health = loomle_payload.get("runtime", {}).get("rpcHealth", {})
        if rpc_health.get("status") not in {"ok", "degraded"}:
            fail(f"loomle rpc health not ready: {loomle_payload}")
        print("[PASS] loomle status query succeeded")

        _ = call_execute_exec_with_retry(
            client=client,
            req_id_base=4,
            code="import unreal\nunreal.log('loomle execute verify')",
        )
        print("[PASS] execute channel is available")

        _ = call_execute_exec_with_retry(
            client=client,
            req_id_base=50,
            code=(
                "import unreal, json\n"
                f"asset='{temp_asset}'\n"
                "pkg_path, asset_name = asset.rsplit('/', 1)\n"
                "asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
                "factory = unreal.BlueprintFactory()\n"
                "factory.set_editor_property('ParentClass', unreal.Actor)\n"
                "bp = asset_tools.create_asset(asset_name, pkg_path, unreal.Blueprint, factory)\n"
                "exists = unreal.EditorAssetLibrary.does_asset_exist(asset)\n"
                "print(json.dumps({'created': bp is not None, 'exists': exists}, ensure_ascii=False))\n"
            ),
        )
        print(f"[PASS] temporary blueprint created: {temp_asset}")

        graph_desc_payload = call_tool(client, 6, "graph", {"graphType": "blueprint"})
        ops = graph_desc_payload.get("ops")
        if not isinstance(ops, list):
            fail("graph payload missing ops[]")
        ops_set = {op for op in ops if isinstance(op, str)}
        if ops_set != EXPECTED_GRAPH_MUTATE_OPS:
            fail(f"graph ops mismatch. expected={sorted(EXPECTED_GRAPH_MUTATE_OPS)} actual={sorted(ops_set)}")
        print("[PASS] graph reports expected mutate ops")

        run_script_args = {
            "graphType": "blueprint",
            "assetPath": temp_asset,
            "graphName": "EventGraph",
            "ops": [
                {
                    "op": "runScript",
                    "args": {
                        "mode": "inlineCode",
                        "entry": "run",
                        "code": "def run(ctx):\n  return {'ok': True, 'assetPath': ctx.get('assetPath', '')}",
                        "input": {"source": "verify_bridge"},
                    },
                }
            ],
        }
        run_script_payload: dict[str, Any] | None = None
        for attempt in range(1, 4):
            payload = call_tool(client, 7, "graph.mutate", run_script_args)
            op_results = payload.get("opResults")
            if isinstance(op_results, list) and op_results:
                first_op = op_results[0] if isinstance(op_results[0], dict) else {}
                script_result = first_op.get("scriptResult")
                if first_op.get("ok") and isinstance(script_result, dict) and script_result.get("ok") is True:
                    run_script_payload = payload
                    break
            if attempt < 3:
                print(f"[WARN] graph.mutate runScript response incomplete (attempt {attempt}/3), retrying...")
                time.sleep(0.3)
                continue
            fail(f"graph.mutate runScript invalid payload={_compact_json(payload)}")
        if run_script_payload is None:
            fail("graph.mutate runScript retry loop ended without payload")
        print("[PASS] graph.mutate runScript inline execution verified")

        print("[PASS] Bridge verification complete")
        return 0
    finally:
        try:
            _ = client.request(
                99,
                "tools/call",
                {
                    "name": "execute",
                    "arguments": {
                        "mode": "exec",
                        "code": (
                            "import unreal\n"
                            f"asset='{temp_asset}'\n"
                            "if unreal.EditorAssetLibrary.does_asset_exist(asset):\n"
                            "  unreal.EditorAssetLibrary.delete_asset(asset)\n"
                        ),
                    },
                },
            )
        except Exception:
            pass
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
