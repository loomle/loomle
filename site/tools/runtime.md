---
layout: default
title: Runtime
parent: Tools
nav_order: 3
---

# Runtime Tools

Runtime tools operate inside the attached Unreal Editor process. Prefer
semantic LOOMLE tools first; use runtime tools for execution, job management,
profiling, and PIE play-session control.

## Tool Summary

| Tool | Purpose |
| --- | --- |
| `execute` | Run Unreal-side Python. |
| `jobs` | Inspect long-running job state, results, and logs. |
| `profiling` | Read official Unreal profiling data families and captures. |
| `play` | Inspect and control PIE sessions. |

## `execute`

Runs Python inside the Unreal Editor process.

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `language` | yes | Currently `python`. |
| `mode` | yes | `exec` for statements, `eval` for expressions. |
| `code` | yes | Python code. |
| `execution.mode` | no | `sync` or `job`. |
| `execution.idempotencyKey` | no | Deduplicate or identify a long-running request. |
| `execution.label` | no | Human-readable job label. |
| `execution.waitMs` | no | Initial wait time for job execution. |

### Boundary

Use public semantic tools when available. Use `execute` for one-off editor
operations, investigation, or gaps that do not yet have a dedicated tool.

## `jobs`

Inspects long-running job state, results, and logs.

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `jobId` | no | Specific job id. |
| `tool` | no | Filter by source tool. |
| `status` | no | Filter by job status. |
| `limit` | no | Limit number of returned jobs. |

## `profiling`

Bridges Unreal profiling data families such as stat groups, ticks, memory
reports, and capture workflows.

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `action` | no | Profiling action to perform. |
| `world` | no | Target world. |
| `target` | no | Session participant target. |
| `target.sessionId` | no | Play/session id. |
| `target.participant` | no | Participant id. |
| `target.role` | no | `server`, `client`, `editor`, or `standalone`. |
| `target.index` | no | Participant index. |
| `group` | no | Profiling group. |
| `capturePath` | no | Capture output path. |

## `play`

Inspects and controls Unreal PIE sessions.

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `action` | yes | `status`, `start`, `stop`, or `wait`. |
| `backend` | no | Currently `pie`. |
| `sessionId` | no | Target play session. |
| `map` | no | Map to play. |
| `ifActive` | no | `error` or `returnStatus`. |
| `timeoutMs` | no | Wait timeout. |
| `participant` | no | Participant id. |
| `role` | no | `server`, `client`, `editor`, or `standalone`. |
| `count` | no | Participant count. |
| `layout` | no | Window layout: `preset`, `originX`, `originY`, `width`, `height`, `gap`. |
| `strict.window` | no | Require window checks. |
| `until` | no | Wait condition for session and participants. |
| `topology` | no | Server/client topology for PIE launch. |

### Recommended Use

Call `play` with `action: status` before starting or stopping sessions. Use
`action: wait` when a workflow depends on PIE reaching a stable state.
