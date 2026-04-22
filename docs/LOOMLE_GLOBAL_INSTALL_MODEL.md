# LOOMLE Global Install Model

## Goal

LOOMLE should be installed once per user account and exposed to agent hosts as a
global MCP server command.

The user should not install a separate `loomle` client inside every Unreal
project. A UE project only needs the `LoomleBridge` project support plugin.

## Non-Goals

- No daemon in the first version.
- No global default project.
- No global active project.
- No Codex or Claude config writer in `loomle`.
- No duplicated CLI surface for capabilities that can be handled through MCP.

## Runtime Shape

Agent hosts configure one stable command:

```text
~/.loomle/bin/loomle mcp
```

Each Codex or Claude session starts its own `loomle mcp` process. That process
is the stdio MCP server for that session only.

The session keeps its attached project in process memory. It does not write a
global active project file and it does not affect other sessions.

```text
Codex / Claude session
  -> ~/.loomle/bin/loomle mcp
      -> session-local attached project
          -> selected UE project endpoint
              -> LoomleBridge MCP runtime inside Unreal Editor
```

## Global Install Layout

```text
~/.loomle/
  bin/
    loomle
  install/
    active.json
  versions/
    <version>/
      loomle
      plugin-cache/
        LoomleBridge/
  state/
    runtimes/
      <runtimeId>.json
  locks/
  logs/
```

There must be no:

```text
~/.loomle/state/default-project.json
~/.loomle/state/active-project.json
```

## Project Support Layout

A UE project receives only project support:

```text
<ProjectRoot>/
  Plugins/
    LoomleBridge/
```

The old per-project client workspace is not part of the target
global model.

## UE Runtime Registration

When a UE project starts and `LoomleBridge` loads, the plugin writes an online
runtime record under:

```text
~/.loomle/state/runtimes/<runtimeId>.json
```

The record is an implementation detail. MCP tools expose it as a project, not as
a runtime.

Minimum record fields:

```json
{
  "schemaVersion": 1,
  "runtimeId": "...",
  "projectId": "...",
  "name": "GameA",
  "projectRoot": "/path/GameA",
  "uproject": "/path/GameA/GameA.uproject",
  "endpoint": "/path/GameA/Intermediate/loomle.sock",
  "platform": "darwin",
  "pid": 12345,
  "pluginVersion": "0.5.0",
  "protocolVersion": 1,
  "startedAt": "...",
  "lastSeenAt": "..."
}
```

An online project is a project whose runtime record is fresh, whose process is
alive, and whose endpoint is connectable.

An offline project is known to LOOMLE but does not currently have a connectable
`LoomleBridge` runtime.

## MCP Tools Before Attach

Before a project is attached, `loomle mcp` exposes only the global/session
project tools:

```text
loomle.status
project.list
project.attach
project.install
```

### `loomle.status`

Returns the current MCP session state:

```json
{
  "loomleVersion": "0.5.0",
  "attached": false,
  "attachedProject": null,
  "onlineProjectCount": 1,
  "projectCount": 2
}
```

### `project.list`

Lists UE projects known to LOOMLE.

Input:

```json
{
  "status": "online | offline | all",
  "includeDiagnostics": false
}
```

Default:

```json
{
  "status": "online",
  "includeDiagnostics": false
}
```

Output entries include enough information for normal project selection:

```json
{
  "projectId": "...",
  "name": "GameA",
  "projectRoot": "/path/GameA",
  "uproject": "/path/GameA/GameA.uproject",
  "status": "online",
  "attachable": true,
  "pluginInstalled": true,
  "pluginVersion": "0.5.0",
  "protocolVersion": 1,
  "lastSeenAt": "...",
  "reason": null
}
```

`status` means runtime availability:

- `online`: Unreal Editor is running with a compatible `LoomleBridge` runtime.
- `offline`: the project is known, but no connectable runtime is currently
  available.

`attached` is session state and belongs in `loomle.status`, not in
`project.list` status semantics.

### `project.attach`

Attaches the current MCP session to one online, compatible project.

Input should support `projectId` first, with `projectRoot` as a convenience
fallback:

```json
{
  "projectId": "..."
}
```

Attach only affects the current `loomle mcp` process.

After attach, the process connects to the selected runtime endpoint:

```text
macOS/Linux: <ProjectRoot>/Intermediate/loomle.sock
Windows:     \\.\pipe\loomle-<hash(projectRoot)>
```

If the project is offline, return an actionable error telling the user to open
the UE project and wait for `LoomleBridge` to report online.

If the plugin/protocol is incompatible, return an actionable error telling the
user to run `project.install`, restart Unreal Editor, then attach again.

### `project.install`

Description:

```text
Install or update LOOMLE support for an Unreal Engine project, including the LoomleBridge plugin and required project settings.
```

Input:

```json
{
  "projectRoot": "/path/GameA",
  "force": false
}
```

Semantics:

- Not installed: install project support.
- Older installed version: update to the active global LOOMLE version.
- Same installed version: no-op.
- Online project: first version should refuse and ask the user to close Unreal
  Editor before changing the loaded plugin.

Return:

```json
{
  "projectRoot": "/path/GameA",
  "pluginPath": "/path/GameA/Plugins/LoomleBridge",
  "changed": true,
  "previousVersion": "0.4.9",
  "installedVersion": "0.5.0",
  "requiresEditorRestart": true,
  "message": "LOOMLE project support installed. Restart Unreal Editor to activate LoomleBridge."
}
```

## MCP Tools After Attach

After `project.attach` succeeds, `loomle mcp` exposes or forwards the actual UE
work tools:

```text
blueprint.*
material.*
pcg.*
editor.*
jobs
diag.*
```

These tools execute against the session-attached project only.

## CLI Surface

The installed `loomle` command should stay small:

```text
loomle mcp
loomle update
loomle doctor
```

No project selection CLI is required in the target model.

### `loomle mcp`

Starts the stdio MCP server used by Codex, Claude, or other MCP hosts.

### `loomle update`

Updates the global LOOMLE install and plugin cache only.

It must not directly modify UE projects. Project support updates are done
through MCP `project.install`.

### `loomle doctor`

Diagnoses the global install and prints actionable information when MCP cannot
start or cannot discover projects.

It should check:

- global install layout
- active version state
- active binary existence
- plugin cache existence
- runtime registry readability/writability
- stale runtime records
- suggested Codex and Claude MCP configuration commands

## Install Flow

First install is owned by the public install script. The script materializes
`~/.loomle`, installs the stable launcher, installs the active version payload,
and prints host configuration commands.

Example output:

```text
Codex:
  codex mcp add loomle -- ~/.loomle/bin/loomle mcp

Claude:
  claude mcp add loomle --scope user ~/.loomle/bin/loomle mcp
```

LOOMLE should not write Codex or Claude configuration in the first version.

## Update Flow

```text
loomle update
  -> acquire global update lock
  -> download manifest
  -> download bundle
  -> verify platform/version/checksum
  -> extract to ~/.loomle/versions/<newVersion>/
  -> atomically update ~/.loomle/install/active.json
  -> release lock
```

The stable `~/.loomle/bin/loomle` launcher hands off to the active version from
`active.json`.

The update path should not overwrite the currently running versioned binary.

## Concurrency Rules

- Multiple agent sessions may run multiple `loomle mcp` processes.
- Multiple sessions may attach to the same online UE project.
- Attached project state is process-local.
- `loomle update` must use a global update lock.
- `project.install` must use a project install lock.
- Same-asset mutate concurrency should be handled separately with asset-level
  locking or optimistic revision checks.

## Migration Direction

The old project-root client install has been removed from the active design.
New work should build on the global install, per-session attach, and
`project.install` model described above.
