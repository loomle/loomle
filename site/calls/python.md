---
layout: default
title: Python Fallback
parent: MCP Calls
nav_order: 7
description: Run Unreal Editor Python only when no structured Loomle interface covers the required capability.
---

# Python Fallback

`python` is Loomle's explicit escape hatch for narrow Unreal Editor operations
that are not yet represented by a structured SAL interface. It runs inside the
bound Editor runtime; it is not a replacement for `sal_query`, `sal_patch`, or
dynamic schema discovery.

Use SAL whenever an installed interface covers the task. SAL preserves exact
object identity, supports schema-guided operations and dry runs, and returns
canonical, verifiable Result Text. Raw Unreal Python does not provide those
guarantees.

## Load Workflow Guidance

Before the first Python call, load the resident general safety workflow:

```text
agent_skill({ name: "use-unreal-python" })
```

It owns live status and project checks, structured-interface selection, API
introspection, idempotent mutation design, exact continuation recovery,
explicit saving, and independent verification.

For PIE or Simulate work, also load:

```text
agent_skill({ name: "debug-unreal-pie-with-python" })
```

The PIE Skill adds only permission, play-session lifecycle, Game World,
cross-frame, and cleanup semantics.

## Run

Pass inline Unreal Python that defines one synchronous `run()` entry point and
returns a JSON-compatible dictionary:

```text
python({
  operation: "run",
  script: "def run():\n    import unreal\n    return {\"engine_version\": unreal.SystemLibrary.get_engine_version()}"
})
```

The result reports `succeeded`, `failed`, `running`, or `lost` and explicitly
states whether Editor state may have changed. Execution remains pinned to the
Editor runtime that accepted the script.

## Poll a Running Execution

If the initial call returns `status: "running"`, it also returns an exact
`python` continuation containing an opaque execution id. Call that continuation
exactly as supplied:

```text
python({ operation: "poll", executionId: "<returned-id>" })
```

Never replay the original script. A runtime restart can make an outstanding
execution `lost`.

## Project Back to Structured Views

A script may mark individual UObjects with `sal.object(value)` anywhere in its
returned dictionary. At terminalization the Bridge replaces each marker in
place with an independent canonical projection record and reports the
projection annex:

```text
python({
  operation: "run",
  script: "def run():\n    import unreal\n    return {\"marked\": sal.object(unreal.Actor)}"
})
```

The replacement record carries `status` (`projected`, `unsupported`, `stale`,
`ambiguous`, or `failed`), `relation` (`exact` or `authored_source`), and for
`projected` views a canonical SAL exact Query result produced by the same
Domain adapters as `sal_query`. The `projection` annex reports `complete`,
`marked`, and `projected` counts; `complete` is false when a projector or
Bridge integrity fault occurred. Projection is read-only: it never loads a map
or asset, changes selection, dirties state, or retains the UObject after the
result is formed. A PIE/SIE duplicate projects `relation: authored_source`
only when its persistent source is uniquely proven.

## Debug Play In Editor

Python remains available while PIE is active. Loomle does not manage PIE or
choose between the Editor World and Game World. Use UE's native
`LevelEditorSubsystem` to request start or stop and
`UnrealEditorSubsystem.get_game_world()` to select the in-process Game World.

PIE transitions are asynchronous. Submit a start or stop request in one short
`run()` and return immediately, then inspect state in a later `python` call.
Never sleep or busy-wait for PIE inside Python: the execution occupies the Game
Thread and prevents the transition or gameplay frame it is waiting for.

The PIE Skill requires user permission before starting unless the current
instruction already explicitly authorizes running or debugging PIE.

## Safety Boundary

`python` is destructive, non-idempotent, and open-world. It has no dry run,
transactional rollback, or safe cancellation after the Editor admits the
script. Inspect the script and obtain any authorization required for its side
effects before running it. Prefer the structured SAL path whenever one exists.
