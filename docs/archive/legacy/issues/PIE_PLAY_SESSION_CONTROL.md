# LOOMLE Local Issue: Play Session Control for Runtime and DS Debugging

## Problem

`LOOMLE` can already inspect runtime state during `PIE`:

- `runtime.isPIE` is surfaced through runtime status
- `execute` remains callable during `PIE`
- `jobs` remains callable during `PIE`
- `profiling` remains callable during `PIE`

That is useful after a play session exists, but it does not solve the workflow
that agents need most:

1. describe the desired runtime topology
2. configure clients, server, windows, and launch settings
3. start the session
4. wait until the expected server and client worlds are ready
5. inspect or profile the right runtime participant
6. stop the session cleanly
7. collect diagnostics and logs when startup or shutdown fails

Today those steps still require ad hoc Unreal Python or manual editor setting
changes. That is fragile for single-player PIE and becomes a real blocker for
dedicated-server debugging.

The product boundary should move from "Python can do it" to "LOOMLE can create
and observe a runtime play session as a first-class product concept."

## Primary Product Use Case

The first-class use case is debugging gameplay behavior with a dedicated server
and one or more clients from the editor.

An agent should be able to:

- create a listen-server or dedicated-server editor play session
- choose the number of clients
- set client window size and placement without manually editing config files
- distinguish server, client, editor, and active worlds
- wait for the server and client worlds to exist before running inspection
- run `execute`, `profiling`, `diagnostic.tail`, and `log.tail` against the
  correct runtime participant
- stop the whole session and verify that it stopped

This is not just a convenience wrapper around the Play button. It is the
runtime test harness for agent-driven gameplay debugging.

## Product Direction

Introduce a top-level play session surface.

Working name:

- `play`

`play` owns session lifecycle and topology:

- start
- stop
- wait
- status
- server/client participant discovery
- session window intent and effective state

`play` should not become the runtime everything-tool.

It should not replace:

- `execute`, which remains the raw Unreal Python fallback
- `profiling`, which remains the official performance data bridge
- `jobs`, which remains the shared long-running lifecycle surface
- future screenshot, input, gameplay-query, or process-control tools

Instead, `play` should define the shared session and participant model that
runtime-aware tools can target.

## Why `play` Instead of `pie`

`PIE` is the first backend, not the whole concept.

The product model should allow future session backends such as:

- editor-hosted PIE
- standalone game from editor
- external dedicated server process
- packaged client process

Calling the public tool `pie` would tightly bind the surface to one Unreal
backend and make future DS or packaged-process workflows feel bolted on.

`play` is the higher-level product concept:

- `backend = "pie"` for the first implementation family
- future backends can extend the same session model
- agents can think in terms of sessions, participants, and targets instead of
  editor implementation details

## Core Model

### Session

A `session` is one runtime play session.

Candidate fields:

- `id`
- `backend`: `pie | standalone | externalProcess | unknown`
- `state`: `inactive | starting | active | ready | stopping | error`
- `startedAt`
- `stoppedAt`
- `map`
- `topology`
- `observability`

### Participant

A `participant` is a runtime actor inside a session.

Common participants:

- `server`
- `client:0`
- `client:1`
- `editor`

Candidate fields:

- `id`
- `role`: `server | client | editor`
- `index`
- `kind`: `dedicated | listen | standalone | client | editor`
- `ready`
- `world`
- `window`
- `capabilities`
- `observability`

### World

A `world` is the Unreal runtime world associated with a participant.

Candidate fields:

- `name`
- `path`
- `worldType`
- `netMode`
- `role`
- `pieInstance`

### Window

A `window` records both the requested window intent and the effective window
state observed after launch.

Candidate fields:

- `requested`
- `effective`
- `warnings`

### Target

A `target` is how other runtime-aware tools select the session participant they
operate on.

Candidate shape:

```json
{
  "target": {
    "sessionId": "optional-session-id",
    "participant": "server"
  }
}
```

Equivalent role/index form:

```json
{
  "target": {
    "role": "client",
    "index": 0
  }
}
```

`worldName` may exist as a fallback for low-level workflows, but the preferred
user intent should be participant-based.

## Candidate Tool Shape

To keep MCP tool schema count small, `play` should be one tool with an `action`
field, not separate `play.start`, `play.stop`, `play.status`, and `play.wait`
tools.

Candidate actions:

- `status`
- `start`
- `stop`
- `wait`

### `action = "status"`

Read-only status for the current or selected play session.

Candidate arguments:

```json
{
  "action": "status",
  "sessionId": "optional-session-id"
}
```

Candidate response:

```json
{
  "status": "ok",
  "session": {
    "id": "pie-20260425-001",
    "backend": "pie",
    "state": "ready"
  },
  "participants": [
    {
      "id": "server",
      "role": "server",
      "kind": "dedicated",
      "ready": true,
      "world": {
        "name": "UEDPIE_0_Map",
        "worldType": "pie",
        "netMode": "dedicatedServer"
      }
    },
    {
      "id": "client:0",
      "role": "client",
      "index": 0,
      "ready": true,
      "world": {
        "name": "UEDPIE_1_Map",
        "worldType": "pie",
        "netMode": "client"
      },
      "window": {
        "requested": { "width": 1280, "height": 720 },
        "effective": { "width": 1280, "height": 720, "x": 20, "y": 80 }
      }
    }
  ],
  "observability": {
    "diagnostics": { "tool": "diagnostic.tail", "fromSeq": 120 },
    "logs": { "tool": "log.tail", "fromSeq": 500 }
  }
}
```

### `action = "start"`

Start a play session from a declarative topology.

Candidate arguments:

```json
{
  "action": "start",
  "backend": "pie",
  "map": "/Game/Maps/TestMap",
  "topology": {
    "server": {
      "kind": "dedicated",
      "launchArgs": "-log"
    },
    "clients": [
      {
        "index": 0,
        "window": { "width": 1280, "height": 720, "x": 20, "y": 80 }
      },
      {
        "index": 1,
        "window": { "width": 1280, "height": 720, "x": 1320, "y": 80 }
      }
    ]
  },
  "wait": {
    "until": "ready",
    "timeoutMs": 30000
  },
  "ifActive": "error"
}
```

Important design point:

- callers describe the desired runtime topology
- LOOMLE maps that intent to Unreal editor play settings or runtime APIs
- callers should not need to know which config file Unreal would normally edit

Possible `ifActive` modes:

- `error`
- `returnStatus`
- `restart`

Initial recommendation:

- support `error` and `returnStatus`
- defer `restart` until startup/shutdown semantics are proven reliable

### `action = "stop"`

Stop the selected or current play session.

Candidate arguments:

```json
{
  "action": "stop",
  "sessionId": "optional-session-id",
  "wait": {
    "until": "inactive",
    "timeoutMs": 30000
  },
  "force": false
}
```

Expected behavior:

- stop all participants in the selected session
- return final `play.status` shape
- surface timeout as a retryable domain error when Unreal is still stopping

### `action = "wait"`

Wait for a session or participant state transition.

Candidate arguments:

```json
{
  "action": "wait",
  "sessionId": "optional-session-id",
  "until": {
    "session": "ready",
    "participants": [
      { "participant": "server", "state": "ready" },
      { "role": "client", "count": 2, "state": "ready" }
    ]
  },
  "timeoutMs": 30000
}
```

This should be a first-class action so agents do not rely on sleeps.

## Dedicated Server Requirements

Dedicated-server debugging should be designed in from the start.

The contract should support:

- one server plus N clients
- dedicated-server and listen-server topology
- server world discovery
- per-client world discovery
- client count verification
- net mode reporting
- server/client role reporting
- window size and placement per client
- optional server launch parameters
- logs that identify server vs client source when possible

Open design question:

- Should dedicated-server sessions be controlled only through Unreal's
  editor-hosted PIE settings first, or should `play` also own external server
  processes from the beginning?

Initial recommendation:

- `backend = "pie"` should be the first backend.
- editor-hosted dedicated-server PIE should be the first DS target.
- keep the schema extensible enough for external server/process control, but do
  not force that complexity into the first backend.

## Window and Layout Design

Window configuration is part of the public contract.

Agents need deterministic window sizing for:

- UI debugging
- screenshot capture
- multi-client layout
- input automation
- visual comparison

The request should support both explicit per-client windows and simple layouts.

Default client window:

```json
{
  "defaultClientWindow": {
    "width": 960,
    "height": 540
  }
}
```

Layout preset:

```json
{
  "layout": {
    "preset": "horizontal",
    "originX": 20,
    "originY": 80,
    "width": 960,
    "height": 540,
    "gap": 16
  }
}
```

Per-client override:

```json
{
  "clients": [
    { "index": 0, "window": { "x": 20, "y": 80 } },
    { "index": 1, "window": { "x": 996, "y": 80 } }
  ]
}
```

Merge order:

1. layout-derived defaults
2. `defaultClientWindow`
3. per-client `window`

Window geometry should be best-effort by default.

The response should include:

- requested geometry
- effective geometry
- warnings if Unreal ignored, clamped, or delayed a setting

Strict mode should be explicit:

```json
{
  "strict": {
    "window": true
  }
}
```

If `strict.window = true`, geometry mismatch may fail the operation with a
structured domain error.

## Relationship to `profiling`

`profiling` should not be merged into `play`.

`play` answers:

- what session exists?
- what participants exist?
- which server/client worlds are ready?
- how do I start, stop, and wait for the session?

`profiling` answers:

- which official Unreal performance data should be collected?
- from which runtime participant?
- with which profiling action and options?

Merging profiling into `play` would make `play` grow into a generic runtime
everything-tool. The same pressure would later pull in screenshots, input,
console commands, gameplay queries, and memory captures.

The better design is:

- `play` is the session and topology authority
- `profiling` is the performance data authority
- both use the same `target` model

Example:

```json
{
  "action": "unit",
  "target": {
    "sessionId": "pie-20260425-001",
    "participant": "server"
  }
}
```

Client profiling:

```json
{
  "action": "game",
  "target": {
    "role": "client",
    "index": 0
  }
}
```

`play.status` may expose participant capabilities to guide agents:

```json
{
  "id": "client:0",
  "role": "client",
  "capabilities": {
    "execute": true,
    "profiling": true,
    "screenshot": true
  }
}
```

The capability hint should guide tool choice, but it should not collapse those
tools into `play`.

## Relationship to `execute`

`execute` should also remain separate.

The long-term goal is for `execute` to understand the shared `target` model
when it is safe to do so:

```json
{
  "mode": "exec",
  "code": "print('server inspection')",
  "target": {
    "participant": "server"
  }
}
```

This should be treated carefully because arbitrary Unreal Python may rely on
editor-global behavior.

Initial recommendation:

- define the shared target model in the play-session design
- make `play.status` the source of participant IDs
- align `profiling` first
- evaluate targeted `execute` after the topology model is stable

## Diagnostics and Logs

`play` should not swallow logs.

It should return observability references:

```json
{
  "observability": {
    "diagnostics": {
      "tool": "diagnostic.tail",
      "fromSeq": 120
    },
    "logs": {
      "tool": "log.tail",
      "fromSeq": 500,
      "filters": {
        "sessionId": "pie-20260425-001"
      }
    }
  }
}
```

Diagnostics summarize stable product-level failures.

Logs preserve noisy engine detail.

Potential domain codes:

- `PLAY_SESSION_ALREADY_ACTIVE`
- `PLAY_SESSION_NOT_ACTIVE`
- `PLAY_START_FAILED`
- `PLAY_STOP_FAILED`
- `PLAY_WAIT_TIMEOUT`
- `PLAY_TOPOLOGY_NOT_READY`
- `PLAY_PARTICIPANT_MISSING`
- `PLAY_CLIENT_COUNT_MISMATCH`
- `PLAY_SERVER_NOT_READY`
- `PLAY_WINDOW_NOT_APPLIED`
- `PLAY_BACKEND_UNSUPPORTED`
- `PLAY_MODE_UNSUPPORTED`
- `PLAY_TARGET_NOT_FOUND`

If logs can be attributed to a participant, they should carry:

```json
{
  "sessionId": "pie-20260425-001",
  "participant": "server"
}
```

That attribution is an enhancement, not a prerequisite for the session model.

## Jobs and Long-Running Behavior

Starting and stopping play sessions can block longer than a normal tool call.

Options:

1. keep `play.start` / `play.stop` synchronous with bounded waits
2. add shared `execution.mode = "job"` support from the start
3. start synchronous, then add job support once lifecycle behavior is stable

Initial recommendation:

- support bounded synchronous calls first
- include `timeoutMs`
- return retryable timeout errors
- design request/response shapes so `execution.mode = "job"` can be added
  later without changing the core contract

## Non-Goals for the Initial Design

The initial design should not try to solve:

- remote machine server deployment
- cloud DS orchestration
- full automation of platform preview modes
- VR preview
- deterministic input automation
- gameplay assertion APIs
- screenshots or visual diffing

Those are valid future features, but they should not make the play-session
surface too large.

## Decisions Needed

1. Is the top-level tool name `play` acceptable, replacing the earlier `pie`
   working name?
2. Should `play` be one tool with `action`, rather than multiple tools?
3. Is `backend = "pie"` the right first backend?
4. Is editor-hosted dedicated-server PIE the correct first DS target?
5. Should startup be described through declarative `topology` rather than
   direct Unreal play-setting fields?
6. Which window settings are required for the first comfortable DS workflow?
7. Should window geometry be best-effort by default with explicit
   `strict.window`?
8. Should `restart` be deferred until lifecycle behavior is proven stable?
9. Should `profiling` remain separate and adopt the shared `target` model?
10. Should targeted `execute` be deferred until `play.status` topology is
    reliable?

## Current Recommended Decisions

1. Use top-level `play`.
2. Use one tool with `action = status | start | stop | wait`.
3. Use `backend = "pie"` as the first backend.
4. Treat editor-hosted dedicated-server PIE as a first-class DS target.
5. Use declarative `topology` as the public request model.
6. Include `width`, `height`, `x`, `y`, layout presets, and per-client
   overrides.
7. Make window geometry best-effort by default, with `strict.window`.
8. Start with `ifActive = error | returnStatus`; defer `restart`.
9. Keep `profiling` separate; add shared `target` support.
10. Define targeted `execute` in the model, but implement it only after
    participant topology is stable.

## Implementation Status

Initial implementation:

- `play.status`
  - reports `session`, `participants`, `observability`, and `diagnostics`
  - discovers editor, server, client, and standalone participants from current
    Unreal world contexts
  - reports world name/path/type, net mode, PIE instance, context handle, and
    readiness
  - reports window `requested`, `effective`, and `warnings` fields when
    geometry is requested or observable
- `play.start`
  - supports `backend = "pie"`
  - requests an in-process PIE session through Unreal editor play APIs
  - supports `topology.server.kind = standalone | listen | dedicated`
  - supports `topology.clientCount`, `topology.clients[]`, `map`,
    `ifActive = error | returnStatus`, `layout`, `defaultClientWindow`, and
    per-client window positions on a best-effort basis
  - merges window intent in order: `layout`, `defaultClientWindow`, then
    per-client `topology.clients[].window`
  - supports `strict.window = true` to turn geometry warnings into
    `PLAY_WINDOW_NOT_APPLIED`
  - returns `state = "starting"` until Unreal creates runtime worlds
- `play.stop`
  - idempotently ends the current PIE session when one exists
  - returns the same status shape
- `play.wait`
  - implemented in the MCP client by polling `play.status`
  - waits for session state and participant conditions without blocking the
    Unreal editor thread
  - supports shorthand waits such as `{ "action": "wait", "role": "client",
    "count": 2 }`
- `profiling`
  - accepts the shared play target model:
    `{ "target": { "participant": "server" } }` or
    `{ "target": { "role": "client", "index": 0 } }`

Known remaining gaps:

- external dedicated server processes are not implemented yet
- targeted `execute` remains deferred
