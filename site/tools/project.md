---
layout: default
title: Project and Session
parent: Tools
nav_order: 1
---

# Project and Session Tools

Project tools establish the MCP session's connection to Unreal Editor. Use
these before asset, Blueprint, Material, PCG, or Widget tools.

## Recommended Flow

1. Call `loomle.status` to see whether the session is already attached.
2. Call `project.list` to find online projects.
3. Call `project.attach` with the target `projectId` or `projectRoot`.
4. Call `loomle` to verify bridge health.
5. Call `context` when you want to start from the user's active UE editor state.

If a project is missing LOOMLE support, close that project's Unreal Editor
instance and call `project.install`.

## Tool Summary

| Tool | Purpose |
| --- | --- |
| `loomle.status` | Report session attachment, project counts, update state, and observability state. |
| `project.list` | List known UE projects, online by default. |
| `project.attach` | Attach this MCP session to one online project. |
| `project.install` | Install or update project-local `Plugins/LoomleBridge`. |
| `schema.inspect` | Read second-level operation schemas for compact edit tools. |
| `loomle` | Check attached bridge health and runtime capabilities. |
| `context` | Read active editor context, active window, and selection. |

## `loomle.status`

Use `loomle.status` before attach or when a session may be stale. It is served
by the client and does not require an attached runtime.

### Parameters

No arguments.

### Returns

| Field | Meaning |
| --- | --- |
| `loomleVersion` | Running LOOMLE client version. |
| `attached` | Whether this MCP session has an attached project. |
| `attachedProject` | Attached project root, or null. |
| `onlineProjectCount` | Number of online projects discovered. |
| `projectCount` | Number of known projects. |
| `update` | Latest-version check and suggested update command. |
| `observability` | Runtime diagnostic/log state when attached. |

## `project.list`

Use `project.list` to discover projects that LOOMLE knows about. Online
projects come from running Unreal Editor instances with `LoomleBridge` loaded.
Offline projects come from the global LOOMLE project registry.

### Parameters

| Field | Required | Values | Notes |
| --- | --- | --- | --- |
| `status` | no | `online`, `offline`, `all` | Defaults to `online`. |
| `includeDiagnostics` | no | boolean | Adds endpoint diagnostics for troubleshooting. |

### Returns

`projects` is an array. Each project includes:

| Field | Meaning |
| --- | --- |
| `projectId` | Stable LOOMLE project id. Prefer this for `project.attach`. |
| `name` | Project display name. |
| `projectRoot` | Project root directory. |
| `uproject` | `.uproject` path when known. |
| `status` | `online` or `offline`. |
| `attachable` | Whether `project.attach` can use this project now. |
| `pluginInstalled` | Whether `Plugins/LoomleBridge` exists. |
| `pluginVersion` | Installed or reported bridge version. |
| `protocolVersion` | Runtime protocol version when reported. |
| `lastSeenAt` | Last known project/runtime timestamp. |
| `reason` | Why the project is not attachable, when relevant. |
| `diagnostics` | Endpoint path and existence when requested. |

### Common Errors

- Invalid `status`: use `online`, `offline`, or `all`.
- Empty result: open the project in Unreal Editor, or run `project.install` if
  the project does not have LOOMLE support.

## `project.attach`

Use `project.attach` after `project.list` returns the target project as online.
Attach state is per MCP session; it is not a global active project.

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `projectId` | one of `projectId` or `projectRoot` | Preferred target from `project.list`. |
| `projectRoot` | one of `projectId` or `projectRoot` | Exact project root string from `project.list`. |

### Returns

| Field | Meaning |
| --- | --- |
| `attached` | Always true on success. |
| `projectId` | Attached project id. |
| `name` | Attached project name. |
| `projectRoot` | Attached project root. |
| `endpoint` | Runtime endpoint used by the client. |

### Common Errors

- `project.attach requires projectId or projectRoot`: pass one target field.
- `No online project matched projectId/projectRoot`: call `project.list` with
  `status: online` and use an exact returned value.
- `Project is not attachable`: inspect the project's `reason` and diagnostics.

## `project.install`

Use `project.install` when a UE project does not yet have LOOMLE support, or
when its project-local bridge needs to be synced to the active global version.

`project.install` copies the active global plugin cache into
`<ProjectRoot>/Plugins/LoomleBridge/`, writes the LOOMLE project registry
record, and applies the required editor support setting. It refuses to run
while that project is online.

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `projectRoot` | yes | Root directory containing the `.uproject` file. |
| `force` | no | Recopy support even if the active version already appears installed. |

### Returns

| Field | Meaning |
| --- | --- |
| `projectRoot` | Installed project root. |
| `pluginPath` | Destination `Plugins/LoomleBridge` path. |
| `changed` | Whether files were copied or updated. |
| `previousVersion` | Previous bridge version when detected. |
| `installedVersion` | Active global LOOMLE version installed into the project. |
| `requiresEditorRestart` | Whether Unreal Editor must be restarted. |
| `message` | Human-readable install result. |

### Common Errors

- `project.install requires projectRoot`: pass a project root.
- Invalid project root: choose the directory that contains the `.uproject`.
- `Project is online`: close Unreal Editor for that project and retry.
- Missing global active install state or plugin cache: reinstall or update
  LOOMLE globally, then retry.

## `schema.inspect`

Use `schema.inspect` only when a tool description says operation-specific
arguments are intentionally omitted from `tools/list`.

### Parameters

| Field | Required | Values | Notes |
| --- | --- | --- | --- |
| `domain` | yes | `blueprint`, `material`, `pcg`, `widget` | Tool family. |
| `tool` | yes | tool name | Must be a tool with second-level schemas. |
| `operation` | no | operation or command name | Narrows to one command schema. |
| `include` | no | `summary`, `schema`, `examples`, `errors`, `notes` | Defaults to summary and schema. |

### Supported Tools

- `blueprint.graph.edit`
- `blueprint.member.edit`
- `blueprint.node.edit`
- `material.graph.edit`
- `pcg.graph.edit`
- `pcg.parameter.edit`
- `widget.tree.edit`

### Common Errors

- Unknown domain: use one of the four supported domains.
- Unknown tool: the selected domain does not provide second-level schemas for
  that tool.
- Unknown operation: call without `operation` first to list available operations.

## `loomle`

Use `loomle` after attach to check whether the Unreal-side bridge is reachable
and healthy.

### Parameters

No arguments.

### Returns

| Field | Meaning |
| --- | --- |
| `status` | Runtime health status. |
| `message` | Error or status message. |
| `runtime.rpcConnected` | Whether the client reached the runtime endpoint. |
| `runtime.listenerReady` | Whether the runtime listener is ready. |
| `runtime.isPIE` | Whether Unreal is in PIE. |
| `runtime.editorBusyReason` | Busy/unavailable reason when reported. |
| `runtime.rpcHealth` | Raw bridge health payload. |
| `runtime.capabilities` | Runtime capability payload when available. |

If no project is attached, `loomle` returns `status: "error"` with
`editorBusyReason: "NO_PROJECT_ATTACHED"`.

## `context`

Use `context` after attach when the user already has an asset open or something
selected in Unreal Editor. It is the fastest way to start from visible editor
state instead of guessing asset paths or graph names.

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `resolveIds` | no | Reserved id resolution input. |
| `resolveFields` | no | Reserved field selection input. |

### Returns

| Field | Meaning |
| --- | --- |
| `timestamp` | UTC snapshot timestamp. |
| `runtime` | Project name, project file path, engine version, editor world, and PIE state. |
| `activeWindow` | Active editor window information. |
| `context` | Active Blueprint, Material, or PCG asset context when detected. |
| `selection` | Selected Blueprint nodes, Material expressions, PCG nodes, or level actors. |

If `context.context` is generic but `selection` has items, use the selection's
asset paths and resolved graph refs to choose the next inspect tool.
