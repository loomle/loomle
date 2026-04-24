#!/usr/bin/env python3
import json
import time
from typing import Any

from test_bridge_smoke import McpStdioClient, call_execute_exec_with_retry, call_tool

SURFACE_NOT_RUN = "not_run"


def blank_surface_matrix() -> dict[str, str]:
    return {
        "mutate": SURFACE_NOT_RUN,
        "queryStructure": SURFACE_NOT_RUN,
        "queryTruth": SURFACE_NOT_RUN,
        "engineTruth": SURFACE_NOT_RUN,
        "verify": SURFACE_NOT_RUN,
        "diagnostics": SURFACE_NOT_RUN,
    }


def compact_json(value: Any, limit: int = 1200) -> str:
    text = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    if len(text) <= limit:
        return text
    return text[: limit - 3] + "..."


def wait_for_bridge_ready(client: McpStdioClient, timeout_s: float = 120.0, interval_s: float = 2.0) -> None:
    deadline = time.time() + timeout_s
    attempt = 0
    while time.time() < deadline:
        attempt += 1
        try:
            loomle = call_tool(client, 9000 + attempt, "loomle", {})
            status = loomle.get("status")
            rpc_health = loomle.get("runtime", {}).get("rpcHealth", {})
            if status not in {"ok", "degraded"} or rpc_health.get("status") not in {"ok", "degraded"}:
                print(f"[WARN] bridge not ready yet (attempt {attempt}): status={status}, rpc={rpc_health}")
                time.sleep(interval_s)
                continue

            _ = call_execute_exec_with_retry(
                client=client,
                req_id_base=9500 + (attempt * 10),
                code="import unreal\nunreal.log('loomle test suite warmup')",
                max_attempts=10,
                retry_delay_s=1.0,
            )
            print(f"[PASS] bridge ready after {attempt} attempt(s)")
            return
        except BaseException as exc:
            print(f"[WARN] bridge readiness probe failed (attempt {attempt}): {exc}")
            time.sleep(interval_s)

    raise RuntimeError(f"bridge did not become ready within {timeout_s:.0f}s")


def blueprint_node_ref(token: dict[str, Any]) -> dict[str, Any]:
    if isinstance(token.get("nodeId"), str):
        return {"id": token["nodeId"]}
    if isinstance(token.get("nodeRef"), str):
        return {"alias": token["nodeRef"]}
    target = token.get("target")
    if isinstance(target, dict):
        return blueprint_node_ref(target)
    raise ValueError(f"Blueprint node reference requires nodeId or nodeRef: {compact_json(token)}")


def blueprint_pin_ref(endpoint: dict[str, Any]) -> dict[str, Any]:
    pin = endpoint.get("pin") or endpoint.get("pinName")
    if not isinstance(pin, str) or not pin:
        raise ValueError(f"Blueprint pin reference requires pin or pinName: {compact_json(endpoint)}")
    return {"node": blueprint_node_ref(endpoint), "pin": pin}


def blueprint_add_node_type(args: dict[str, Any]) -> dict[str, Any]:
    function_name = args.get("functionName")
    if isinstance(function_name, str) and function_name:
        node_type: dict[str, Any] = {"functionName": function_name}
        function_class = args.get("functionClassPath")
        if isinstance(function_class, str) and function_class:
            node_type["functionClassPath"] = function_class
        return node_type

    node_class = args.get("nodeClassPath")
    if node_class == "/Script/BlueprintGraph.K2Node_IfThenElse":
        return {"kind": "branch"}
    if node_class == "/Script/BlueprintGraph.K2Node_ExecutionSequence":
        return {"kind": "sequence"}
    if isinstance(node_class, str) and node_class:
        node_type = {"id": f"class:{node_class}"}
        for key, value in args.items():
            if key not in {"nodeClassPath", "position"}:
                node_type[key] = value
        return node_type

    raise ValueError(f"Blueprint addNode.byClass requires nodeClassPath or functionName: {compact_json(args)}")


def blueprint_command_from_legacy_op(op: dict[str, Any]) -> dict[str, Any] | None:
    op_name = op.get("op")
    args = op.get("args")
    if not isinstance(args, dict):
        args = {}

    if op_name == "layoutGraph":
        return None
    if op_name == "addNode.byClass":
        command: dict[str, Any] = {"kind": "addNode", "nodeType": blueprint_add_node_type(args)}
        client_ref = op.get("clientRef")
        if isinstance(client_ref, str) and client_ref:
            command["alias"] = client_ref
        position = args.get("position")
        if isinstance(position, dict):
            command["position"] = position
        return command
    if op_name == "removeNode":
        target = args.get("target")
        if not isinstance(target, dict):
            target = args
        return {"kind": "removeNode", "node": blueprint_node_ref(target)}
    if op_name in {"connectPins", "disconnectPins"}:
        from_endpoint = args.get("from")
        to_endpoint = args.get("to")
        if not isinstance(from_endpoint, dict) or not isinstance(to_endpoint, dict):
            raise ValueError(f"{op_name} requires from/to endpoints: {compact_json(op)}")
        return {
            "kind": "connect" if op_name == "connectPins" else "disconnect",
            "from": blueprint_pin_ref(from_endpoint),
            "to": blueprint_pin_ref(to_endpoint),
        }
    if op_name == "breakPinLinks":
        target = args.get("target")
        if not isinstance(target, dict):
            raise ValueError(f"breakPinLinks requires target endpoint: {compact_json(op)}")
        return {"kind": "breakLinks", "target": blueprint_pin_ref(target)}
    if op_name == "setPinDefault":
        target = args.get("target")
        if not isinstance(target, dict):
            raise ValueError(f"setPinDefault requires target endpoint: {compact_json(op)}")
        return {"kind": "setPinDefault", "target": blueprint_pin_ref(target), "value": args.get("value")}

    raise ValueError(f"Unsupported Blueprint legacy op in test payload: {op_name}")


def blueprint_commands_from_legacy_payload(payload: dict[str, Any]) -> list[dict[str, Any]]:
    ops = payload.get("ops")
    if not isinstance(ops, list):
        raise ValueError(f"Blueprint legacy payload requires ops[]: {compact_json(payload)}")
    commands: list[dict[str, Any]] = []
    for op in ops:
        if not isinstance(op, dict):
            raise ValueError(f"Blueprint legacy op must be an object: {compact_json(op)}")
        command = blueprint_command_from_legacy_op(op)
        if command is not None:
            commands.append(command)
    return commands


def blueprint_edit_args_from_legacy_payload(payload: dict[str, Any]) -> dict[str, Any]:
    edit_args = {
        key: value
        for key, value in payload.items()
        if key not in {"tool", "graphType", "ops"}
    }
    edit_args["commands"] = blueprint_commands_from_legacy_payload(payload)
    return edit_args
