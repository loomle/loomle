---
layout: default
title: Runtime
parent: Tools
nav_order: 3
---

# Runtime Tools

Runtime tools operate inside the attached Unreal Editor process. Use them for
Python execution, long-running jobs, profiling, and PIE play sessions.

## Tool List

- `execute`: run Unreal-side Python.
- `jobs`: inspect long-running job state, results, and logs.
- `profiling`: read official Unreal profiling data families.
- `play`: inspect and control PIE play sessions.

## Schemas

| Tool | Required | Key Fields |
| --- | --- | --- |
| `execute` | `language`, `mode`, `code` | `language: python`, `mode: exec/eval`, `execution.mode: sync/job` |
| `jobs` | none | `jobId`, `tool`, `status`, `limit` |
| `profiling` | none | `action`, `world`, `target`, `group`, `capturePath` |
| `play` | `action` | `action: status/start/stop/wait`, `backend: pie`, `map`, `timeoutMs`, `topology`, `until` |

Use `execute` sparingly. Prefer semantic LOOMLE tools when a public tool exists,
and reserve Python for one-off editor operations or diagnostics.
