# LOOMLE Local Issue: Top-Level `jobs` Runtime for Long-Running Tasks

## Problem

`LOOMLE` currently treats long-running Unreal work as if it were still a
single synchronous tool call.

That is acceptable for short tasks, but it breaks down for common editor
operations that legitimately take much longer, such as:

- large asset builds
- Nanite enable or rebuild flows
- spline bake pipelines
- package save operations
- other editor-side batch work

The key failure mode is not only that a caller hits a timeout. The deeper
problem is that Unreal often continues processing after the response window has
expired.

That creates an unsafe contract:

- the caller sees timeout and assumes failure
- the editor may still be running the original task
- retries can duplicate expensive or destructive work
- later success has to be inferred from logs instead of from a stable product
  surface

This is a product-model problem, not just a timeout-tuning problem.

## Product Direction

Do not create a separate long-task tool for each feature area.

Instead, introduce one shared job runtime and one shared top-level job
management interface:

1. original tools stay responsible for intent
- `execute`
- `graph.mutate`
- `graph.verify`
- future long-running tools

2. a top-level `jobs` tool manages lifecycle
- `status`
- `result`
- `logs`
- `list`
- later `forget` or `cancel`

Long-running work should be requested from the original tool through a shared
execution mode switch.

This should become the standard `LOOMLE` model for Unreal-side automation:

- use `execute` for short tasks
- use `execution.mode = "job"` when a task should enter the shared runtime
- use top-level `jobs` to inspect or collect outcomes later

## Why Top-Level `jobs`

This is preferable to a tool-specific long-task surface such as
`execute.jobs` because:

1. the capability is more general than `execute`
- graph tools may also need long-running execution
- future tools should not invent their own task protocols

2. the original tool still owns the business intent
- callers still invoke `execute` when they want Unreal Python
- callers still invoke `graph.verify` when they want verification
- only the execution mode changes

3. lifecycle becomes uniform
- every long task can share one `jobId`
- one `jobs` interface can serve every tool family

## Core Design Rule

For long-running Unreal work, success should mean:

- the task was accepted
- the task is trackable
- the final result can be read later

It should not mean:

- the task finished before the request deadline

## Decisions Locked for This Issue

The following decisions should be treated as settled for the first version:

1. `jobs` is a top-level tool
- it does not live under `execute`
- it does not live under `graph`

2. original tools submit their own work
- long-task intent stays on the original tool
- lifecycle inspection moves to `jobs`

3. job mode is opt-in through `execution.mode = "job"`
- tools do not become job-capable by accident
- each adopter must opt in explicitly

4. the MVP starts with `execute`
- the runtime is generic
- the first public adopter stays narrow

5. multiple jobs may exist, but execution stays serial first
- registry concurrency is allowed
- Unreal runner concurrency is intentionally constrained

## Non-Goals

The first version should **not** try to solve:

- hard cancellation guarantees
- old-version backward compatibility
- arbitrary multi-language execution
- perfect percentage progress for every Unreal task
- unbounded concurrent execution

## Tool Shape

Use one new top-level tool:

- `jobs`

Use an `action` field inside the tool.

The original tool performs submission. The `jobs` tool performs lifecycle
inspection and retrieval.

First-version `jobs` actions:

1. `status`
2. `result`
3. `logs`
4. `list`

Optional later actions:

- `forget`
- `cancel`

## Shared Execution Envelope

Long-running behavior should not be hardcoded separately inside each tool.

Instead, every tool that wants job-mode execution should adopt one shared
execution envelope.

### Proposed Shape

```json
{
  "execution": {
    "mode": "sync|job",
    "idempotencyKey": "string",
    "label": "optional-human-readable-label",
    "waitMs": 1000,
    "resultTtlMs": 3600000
  }
}
```

### Field Rules

1. `mode`
- `sync` is the default
- `job` requests submission into the shared job runtime

2. `idempotencyKey`
- required when `mode = "job"`
- optional or ignored when `mode = "sync"`

3. `label`
- optional
- should be short and task-oriented
- improves job listing and log readability

4. `waitMs`
- caller-side submission wait budget
- does not represent full Unreal task lifetime
- should only control how long the caller waits for acceptance and initial
  acknowledgement

5. `resultTtlMs`
- optional retention budget for final result and logs
- runtime may clamp it to configured limits

### Envelope Design Rule

Tool-specific business arguments stay where they already belong.

Only long-task lifecycle concerns go into `execution`.

## First-Version Contract

### 1. Job Submission Through Existing Tools

Purpose:

- register a long-running Unreal task
- return a durable `jobId`
- do not require task completion before returning from the original tool

Example with `execute`:

```json
{
  "language": "python",
  "mode": "exec",
  "code": "string",
  "execution": {
    "mode": "job",
    "idempotencyKey": "string",
    "label": "optional-human-readable-label",
    "waitMs": 1000,
    "resultTtlMs": 3600000
  }
}
```

Notes:

- `execution.mode = "sync"` remains the default behavior.
- `execution.mode = "job"` requests registration in the shared job runtime.
- `waitMs` here means how long the caller is willing to wait for submission
  acknowledgement, not how long the Unreal task may run.
- `idempotencyKey` should be required for the first version.

Example submission response from the original tool:

```json
{
  "jobId": "string",
  "status": "queued|running",
  "acceptedAt": "RFC3339",
  "idempotencyKey": "string",
  "pollAfterMs": 1000
}
```

The same submission pattern should be available to any tool that opts into the
shared job runtime later.

### Submission Result Rules

When a tool accepts `execution.mode = "job"`:

- the top-level call itself should succeed if submission succeeded
- submission should not wait for full Unreal completion
- the tool should return a compact job-acceptance payload rather than pretending
  to be a synchronous result

Suggested common acceptance fields:

```json
{
  "jobId": "string",
  "status": "queued|running",
  "acceptedAt": "RFC3339",
  "pollAfterMs": 1000
}
```

This should be shared across all job-capable tools, not reinvented per tool.

### Submission Versus Synchronous Result

This distinction must stay explicit.

When `execution.mode = "sync"`:

- the original tool returns its normal business result
- no `job` object is required

When `execution.mode = "job"`:

- the original tool returns job acceptance only
- it should not also pretend to contain the final business result
- final business result belongs to `jobs.result`

This rule keeps caller logic simple:

- submit on the original tool
- inspect through `jobs`
- collect the final payload through `jobs.result`

### 2. `jobs` with `action = "status"`

Purpose:

- inspect current lifecycle state without fetching the full result

Input:

```json
{
  "action": "status",
  "jobId": "string"
}
```

Output:

```json
{
  "jobId": "string",
  "status": "queued|running|succeeded|failed",
  "acceptedAt": "RFC3339",
  "startedAt": "RFC3339",
  "finishedAt": "RFC3339",
  "heartbeatAt": "RFC3339",
  "resultAvailable": true,
  "message": "optional-status-summary",
  "logCursor": "opaque-cursor"
}
```

Rules:

- `status` must be cheap enough to poll frequently
- `status` should not require downloading result payloads or full logs
- `heartbeatAt` should advance while the runtime still has evidence that the
  job is alive

### 3. `jobs` with `action = "result"`

Purpose:

- fetch final result after completion

Input:

```json
{
  "action": "result",
  "jobId": "string"
}
```

Output:

```json
{
  "jobId": "string",
  "status": "queued|running|succeeded|failed",
  "resultAvailable": true,
  "result": {},
  "stdout": "string",
  "error": {
    "code": "string",
    "message": "string",
    "detail": "string"
  }
}
```

Rules:

- if the task is still `queued` or `running`, that is not a failure
- unfinished tasks should return their real current state
- callers should not have to infer progress from a timeout
- once a task reaches `succeeded` or `failed`, `result` should stay stable for
  the remainder of the retention window

### 4. `jobs` with `action = "logs"`

Purpose:

- read incremental task logs

Input:

```json
{
  "action": "logs",
  "jobId": "string",
  "cursor": "opaque-cursor"
}
```

Output:

```json
{
  "jobId": "string",
  "entries": [
    {
      "time": "RFC3339",
      "level": "info|warning|error",
      "message": "string"
    }
  ],
  "nextCursor": "opaque-cursor",
  "hasMore": true
}
```

Rules:

- logs should be incremental
- logs should be ordered
- `cursor` should be opaque and runtime-owned
- callers should not assume logs are complete until the job reaches a terminal
  state

### 5. `jobs` with `action = "list"`

Purpose:

- inspect currently known jobs
- recover outstanding work after reconnect or session loss

Input:

```json
{
  "action": "list",
  "status": "optional-status-filter",
  "limit": 100
}
```

Output:

```json
{
  "jobs": [
    {
      "jobId": "string",
      "label": "string",
      "status": "queued|running|succeeded|failed",
      "acceptedAt": "RFC3339"
    }
  ]
}
```

Rules:

- `list` is for recovery and situational awareness
- it should not replace `status` for precise tracking
- first version may keep the filter surface narrow

## First-Version `jobs` Action Set

The MVP should stay disciplined.

Required in v1:

1. `status`
2. `result`
3. `logs`
4. `list`

Not required in v1:

1. `forget`
2. `cancel`
3. `retry`
4. `pause`
5. `resume`

This keeps the first version focused on safe observability rather than on
control-plane complexity.

## Job State Model

Keep the first version intentionally small:

1. `queued`
2. `running`
3. `succeeded`
4. `failed`

Do not add more states until they unlock a concrete product need.

## Concurrency Model

The shared job runtime should support multiple jobs existing at the same time.

However, first-version execution policy should stay conservative:

- multiple jobs may be submitted
- the runtime may queue them
- first-version default execution should be serial

This means:

- many jobs can exist
- not all jobs need to run concurrently

Why:

- Unreal asset operations often share editor state
- many long tasks touch overlapping assets or save paths
- uncontrolled parallelism would make reliability worse before it makes it
  better

Future expansion may add:

- `maxConcurrentJobs`
- `concurrencyGroup`
- read-only versus write-heavy scheduling classes

But none of those should be required for MVP.

## Error Model

The shared job runtime should separate:

1. submission failure
- the runtime could not accept the job at all

2. runtime execution failure
- the job was accepted
- later failed while running

3. result unavailability
- the job exists
- but a final result is not yet ready

### Submission Failure

Submission failure should still be reported directly on the original tool call.

Examples:

- invalid arguments
- missing `idempotencyKey`
- unsupported `execution.mode = "job"` on that tool
- runtime queue unavailable

### Runtime Failure

Runtime failure should be represented by:

- terminal `status = failed`
- stable terminal error payload inside `jobs.result`

### Not-Yet-Ready Result

This should not be reported as an error.

Instead:

- `jobs.result` should return `status = queued|running`
- `resultAvailable = false`

### Job Lookup Failures

Top-level `jobs` calls should distinguish lifecycle lookup problems from job
execution failure.

Suggested error classes:

1. `JOB_NOT_FOUND`
- the `jobId` is unknown to the runtime

2. `JOB_RESULT_EXPIRED`
- the job once existed
- retention has elapsed
- final result or logs are no longer available

3. `JOB_ACTION_UNSUPPORTED`
- the runtime recognizes `jobs`
- the requested `action` is not implemented in this version

These are control-plane failures, not business-task failures.

## First Adopters

The first rollout should stay narrow.

### Phase 1 Adopter

1. `execute`

Why:

- this is the clearest current pain point
- long Unreal Python tasks already exist in the field
- issue `#132` is specifically about this lane

### Explicit Non-Adopters for MVP

These should **not** be required in the first implementation:

1. `graph.mutate`
2. `graph.verify`
3. `editor.open`
4. `editor.focus`
5. `editor.screenshot`

Why:

- they do not need to block the job-runtime foundation
- keeping MVP narrow reduces rollout risk

### Phase 2 Candidates

After `execute` stabilizes, evaluate:

1. `graph.verify`
- if large compile-backed verification becomes a real long-task need

2. `graph.mutate`
- only for cases where a structured mutate flow intentionally delegates into a
  long-running runtime process

The design rule should stay:

- first build the shared runtime once
- then let other tools opt in intentionally

## MCP and RPC Impact

This issue does not yet change the active wire contract.

But the likely implementation direction is:

1. MCP layer
- add top-level `jobs`
- extend selected tools with optional `execution`

2. RPC layer
- preserve current direct tool names
- allow the Unreal runtime to recognize `execution.mode = "job"`
- add runtime support for `jobs` actions

The design rule should be:

- one lifecycle contract at MCP
- one lifecycle contract at RPC
- no tool-specific job protocol forks

## Protocol Draft: Shared `execution` Fields

The first implementable step should define one narrow shared schema for
job-capable tool submission.

### MVP Submission Shape

```json
{
  "execution": {
    "mode": "sync|job",
    "idempotencyKey": "string",
    "label": "string",
    "waitMs": 1000,
    "resultTtlMs": 3600000
  }
}
```

### Required vs Optional in MVP

When `execution.mode = "sync"`:

- `mode` optional
- `idempotencyKey` ignored
- `label` optional
- `waitMs` optional
- `resultTtlMs` ignored

When `execution.mode = "job"`:

- `mode` required
- `idempotencyKey` required
- `label` optional
- `waitMs` optional
- `resultTtlMs` optional

### Recommended Validation Rules

1. reject empty `idempotencyKey` for `job` mode
2. reject `resultTtlMs <= 0`
3. reject `waitMs <= 0`
4. clamp very large `waitMs` and `resultTtlMs` to runtime-owned limits
5. reject `execution.mode = "job"` on tools that have not explicitly opted in

## Protocol Draft: Submission Response

Every tool that accepts `execution.mode = "job"` should return the same common
acceptance shape.

### Suggested Shape

```json
{
  "job": {
    "jobId": "string",
    "status": "queued|running",
    "acceptedAt": "RFC3339",
    "idempotencyKey": "string",
    "pollAfterMs": 1000
  }
}
```

### Why Nest Under `job`

This keeps the response extensible:

- tool-specific acceptance metadata can be added beside it
- the caller can always detect job-mode acceptance by presence of `job`
- future job-capable tools can reuse exactly the same response fragment

### Idempotency Rule

If the same `idempotencyKey` is submitted again for the same tool and equivalent
business payload:

- the runtime should return the existing `jobId`
- it should not create a duplicate running job

### Suggested Tool-Level Rejection Codes

When a caller requests `execution.mode = "job"` and submission cannot be
accepted, the original tool should fail directly with one of a small set of
stable errors.

Suggested codes:

1. `JOB_MODE_UNSUPPORTED`
- this tool has not opted into job mode

2. `INVALID_EXECUTION_ENVELOPE`
- `execution` exists but is malformed

3. `IDEMPOTENCY_KEY_REQUIRED`
- caller requested job mode without a valid key

4. `JOB_RUNTIME_UNAVAILABLE`
- the shared runtime could not accept new work

## Protocol Draft: Top-Level `jobs` Input Schemas

The top-level `jobs` tool should stay intentionally small.

### `jobs` `action = "status"`

Suggested input schema:

```json
{
  "type": "object",
  "required": ["action", "jobId"],
  "properties": {
    "action": { "const": "status" },
    "jobId": { "type": "string", "minLength": 1 }
  },
  "additionalProperties": false
}
```

### `jobs` `action = "result"`

Suggested input schema:

```json
{
  "type": "object",
  "required": ["action", "jobId"],
  "properties": {
    "action": { "const": "result" },
    "jobId": { "type": "string", "minLength": 1 }
  },
  "additionalProperties": false
}
```

### `jobs` `action = "logs"`

Suggested input schema:

```json
{
  "type": "object",
  "required": ["action", "jobId"],
  "properties": {
    "action": { "const": "logs" },
    "jobId": { "type": "string", "minLength": 1 },
    "cursor": { "type": "string" },
    "limit": { "type": "integer", "minimum": 1, "maximum": 1000 }
  },
  "additionalProperties": false
}
```

### `jobs` `action = "list"`

Suggested input schema:

```json
{
  "type": "object",
  "required": ["action"],
  "properties": {
    "action": { "const": "list" },
    "status": {
      "type": "string",
      "enum": ["queued", "running", "succeeded", "failed"]
    },
    "tool": { "type": "string" },
    "sessionId": { "type": "string" },
    "limit": { "type": "integer", "minimum": 1, "maximum": 1000 }
  },
  "additionalProperties": false
}
```

## Protocol Draft: Top-Level `jobs` Output Schemas

### `jobs.status`

Suggested stable fields:

```json
{
  "jobId": "string",
  "tool": "string",
  "status": "queued|running|succeeded|failed",
  "acceptedAt": "RFC3339",
  "startedAt": "RFC3339",
  "finishedAt": "RFC3339",
  "heartbeatAt": "RFC3339",
  "resultAvailable": true,
  "message": "string",
  "logCursor": "string"
}
```

### `jobs.result`

Suggested stable fields:

```json
{
  "jobId": "string",
  "tool": "string",
  "status": "queued|running|succeeded|failed",
  "resultAvailable": true,
  "result": {},
  "stdout": "string",
  "error": {
    "code": "string",
    "message": "string",
    "detail": "string"
  }
}
```

### `jobs.logs`

Suggested stable fields:

```json
{
  "jobId": "string",
  "entries": [
    {
      "time": "RFC3339",
      "level": "info|warning|error",
      "message": "string"
    }
  ],
  "nextCursor": "string",
  "hasMore": true
}
```

### `jobs.list`

Suggested stable fields:

```json
{
  "jobs": [
    {
      "jobId": "string",
      "tool": "string",
      "label": "string",
      "status": "queued|running|succeeded|failed",
      "acceptedAt": "RFC3339",
      "sessionId": "string"
    }
  ]
}
```

## Wire-Contract Recommendation

The first implementation should keep the wire contract narrow and predictable.

### 1. Original Tool Submission

Original tools should not change their top-level name.

Examples:

- `execute`
- future `graph.verify`
- future `graph.mutate`

They should only gain one optional `execution` field.

### 2. Top-Level `jobs` Tool

`jobs` should be added as one new top-level tool with an `action` field.

Recommended action enum in the MCP schema:

- `status`
- `result`
- `logs`
- `list`

### 3. One Stable Acceptance Fragment

Every tool that supports `execution.mode = "job"` should return the same job
acceptance fragment:

```json
{
  "job": {
    "jobId": "string",
    "status": "queued|running",
    "acceptedAt": "RFC3339",
    "idempotencyKey": "string",
    "pollAfterMs": 1000
  }
}
```

The runtime should not create per-tool acceptance variants in v1.

### 4. One Stable `jobs` Action Surface

The `jobs` tool should always route through the same argument shape:

```json
{
  "action": "status|result|logs|list",
  "...": "action-specific fields"
}
```

This keeps MCP and RPC aligned around one dispatch model.

## Protocol Draft: MCP Schema Guidance

### `execute`

The active `execute` schema should gain an optional `execution` object.

Recommended MVP fields:

```json
{
  "execution": {
    "type": "object",
    "properties": {
      "mode": {
        "type": "string",
        "enum": ["sync", "job"]
      },
      "idempotencyKey": {
        "type": "string",
        "minLength": 1
      },
      "label": {
        "type": "string",
        "minLength": 1
      },
      "waitMs": {
        "type": "integer",
        "minimum": 1
      },
      "resultTtlMs": {
        "type": "integer",
        "minimum": 1
      }
    },
    "additionalProperties": false
  }
}
```

Validation rule:

- `idempotencyKey` becomes required only when `mode = "job"`

### `jobs`

The top-level `jobs` MCP schema should be a discriminated-by-action object.

Suggested v1 shape:

```json
{
  "type": "object",
  "required": ["action"],
  "properties": {
    "action": {
      "type": "string",
      "enum": ["status", "result", "logs", "list"]
    },
    "jobId": {
      "type": "string",
      "minLength": 1
    },
    "cursor": {
      "type": "string"
    },
    "limit": {
      "type": "integer",
      "minimum": 1,
      "maximum": 1000
    },
    "status": {
      "type": "string",
      "enum": ["queued", "running", "succeeded", "failed"]
    },
    "tool": {
      "type": "string"
    },
    "sessionId": {
      "type": "string"
    }
  },
  "additionalProperties": false
}
```

Action-specific required-field validation can stay in runtime code for v1 as
long as MCP keeps the tool discoverable and the error messages stay explicit.

## Protocol Draft: RPC Routing Guidance

The RPC layer should mirror the MCP model instead of inventing a second
long-task grammar.

Recommended direction:

1. original tool names remain unchanged at RPC
- `execute` keeps routing as `execute`
- future adopters keep their own names

2. `execution` travels inside the original RPC args object
- the runtime decides whether to execute synchronously or register a job

3. `jobs` becomes one new RPC-visible tool
- action-dispatch happens in runtime code
- action names should match MCP exactly

The design goal is:

- no MCP-only job semantics
- no RPC-only job semantics
- one shared lifecycle contract end to end

## Response Envelope Guidance

The first implementation should keep response envelope rules simple.

### Synchronous Calls

When `execution.mode` is omitted or set to `sync`:

- return the tool's normal business payload
- do not inject a `job` field unless the tool has actually entered job mode

### Job Submission Calls

When `execution.mode = "job"`:

- return the `job` acceptance fragment
- do not also include the final business payload

### `jobs` Calls

The `jobs` tool should return direct action payloads, not another nested layer.

Examples:

- `jobs.status` returns status fields directly
- `jobs.result` returns result fields directly
- `jobs.logs` returns log fields directly

This keeps polling code lighter and avoids envelope drift between actions.

## Integration Order Recommendation

The safest rollout order is:

1. finalize the contract in docs
2. add MCP schema for `jobs`
3. add MCP schema support for `execution` on `execute`
4. add RPC/runtime registry plumbing
5. enable `execute` job-mode submission
6. only then teach agents and tests to rely on it

This order keeps product semantics ahead of implementation shortcuts.

## Open Questions Worth Deferring

These questions are real, but should stay explicitly out of the MVP decision
path:

1. whether `jobs.forget` should exist in v1 or v2
2. whether terminal logs should also be embedded inside `jobs.result`
3. whether `list` should default to active jobs only
4. whether job history should survive Unreal restart
5. whether future graph tools should expose deferred results directly in their
   own payloads

The first version does not need answers to all of these before implementation
can begin.

## Protocol Draft: End-to-End Examples

### Example 1: Submit a Long `execute` Job

```json
{
  "tool": "execute",
  "args": {
    "language": "python",
    "mode": "exec",
    "code": "build_large_nanite_batch()",
    "execution": {
      "mode": "job",
      "idempotencyKey": "nanite-batch-20260325-001",
      "label": "nanite_enable_batch",
      "waitMs": 1000,
      "resultTtlMs": 3600000
    }
  }
}
```

Example acceptance:

```json
{
  "job": {
    "jobId": "job_7f3d9b6e",
    "status": "queued",
    "acceptedAt": "2026-03-25T15:20:31Z",
    "idempotencyKey": "nanite-batch-20260325-001",
    "pollAfterMs": 1000
  }
}
```

### Example 2: Poll Status

```json
{
  "tool": "jobs",
  "args": {
    "action": "status",
    "jobId": "job_7f3d9b6e"
  }
}
```

Example status:

```json
{
  "jobId": "job_7f3d9b6e",
  "tool": "execute",
  "status": "running",
  "acceptedAt": "2026-03-25T15:20:31Z",
  "startedAt": "2026-03-25T15:20:34Z",
  "heartbeatAt": "2026-03-25T15:21:02Z",
  "resultAvailable": false,
  "message": "Building static meshes",
  "logCursor": "cursor_128"
}
```

### Example 3: Read Incremental Logs

```json
{
  "tool": "jobs",
  "args": {
    "action": "logs",
    "jobId": "job_7f3d9b6e",
    "cursor": "cursor_128",
    "limit": 200
  }
}
```

### Example 4: Fetch Final Result

```json
{
  "tool": "jobs",
  "args": {
    "action": "result",
    "jobId": "job_7f3d9b6e"
  }
}
```

Example terminal result:

```json
{
  "jobId": "job_7f3d9b6e",
  "tool": "execute",
  "status": "succeeded",
  "resultAvailable": true,
  "result": {
    "status": "ok",
    "changedAssets": 417
  },
  "stdout": "Batch complete"
}
```

## MVP Field Discipline

The MVP should be strict about which fields are truly required.

### Must Be Required in v1

1. `execution.mode` when the caller wants job mode
2. `execution.idempotencyKey` in job mode
3. `jobs.action`
4. `jobs.jobId` for `status`, `result`, and `logs`
5. terminal `status` in every `jobs` response

### Should Stay Optional in v1

1. `label`
2. `sessionId`
3. `traceId`
4. `requestId`
5. `message`
6. `stdout`
7. `limit` on `logs` and `list`

### Should Stay Out of v1

1. progress percentage guarantees
2. cancellation guarantees
3. per-job priority
4. pause/resume
5. arbitrary dependency graphs between jobs

## Session Relationship

The job runtime should not depend on session mode in order to exist.

But jobs should still be able to record session linkage when available.

Recommended metadata:

- `sessionId` optional
- `requestId` optional
- `traceId` optional
- `label`
- `idempotencyKey`

Design rule:

- session is a collaboration context
- job is a runtime task lifecycle
- a job may belong to a session
- a job must still survive without one

## Internal Runtime Model

### 1. Job Registry

Maintain a registry with at least:

- `jobId`
- request summary
- code hash
- `idempotencyKey`
- status
- timestamps
- final result or final error
- log buffer
- session metadata when present

Preferred additional fields:

- `toolName`
- tool argument summary
- `waitMs`
- `resultTtlMs`
- `lastError`
- `lastHeartbeatAt`
- `completionSummary`

### 2. Runner

The runner should:

- pick queued jobs
- move them into `running`
- execute them inside Unreal
- update heartbeat and logs
- write final state into the registry

Runner rule:

- first-version runners should be serial by default
- queue order should be deterministic

### 2a. Submission Boundary

Submission should stop at the point where the runtime has:

- validated the `execution` envelope
- normalized the business payload
- allocated or reused a `jobId`
- persisted initial registry state

The original tool should not block on full Unreal execution after this point.

### 3. Result Retention

Jobs should live long enough for reconnect and follow-up polling.

First version only needs bounded retention, not permanent history.

### 4. Logging

Long tasks need product-native log access.

Callers should not have to scrape project logs to know whether a job is still
alive.

## Relationship to Existing Tools

`execute` should remain:

- the primary short-task Unreal Python surface
- synchronous by default
- simple for interactive reads and quick edits

`graph.*` should remain:

- the structured graph-native surface
- graph-specific in intent
- eligible to opt into `execution.mode = "job"` later if needed

The design goal is not to deprecate existing tools.

The goal is to stop forcing every long task into a synchronous request model.

## Suggested First Implementation Cut

The smallest useful implementation is:

1. add optional `execution` to `execute`
2. support `execution.mode = "job"` only on `execute`
3. add top-level `jobs`
4. implement `status`, `result`, `logs`, and `list`
5. keep one running job at a time

This cut is small enough to ship, but already solves the false-negative timeout
problem for long Unreal Python tasks.

## Implementation Slice Proposal

To keep rollout clean, implementation should be cut into small layers:

1. shared job DTOs and status enums
2. job registry plus retention rules
3. top-level `jobs` tool routing
4. `execute` submission in `execution.mode = "job"`
5. first serial runner
6. logs capture and final result persistence

This ordering reduces the chance that `execute` grows its own temporary
long-task protocol before the shared runtime exists.

## Rollout

### Phase A: Contract

- define a shared `execution.mode = "job"` contract
- define top-level `jobs`
- define state model, result retrieval, and log retrieval
- define serial first-version scheduling

### Phase B: Unreal-side Registry MVP

- register jobs
- allow `execute` to submit into the runtime
- track one running job at a time
- expose top-level `jobs.status`, `jobs.result`, and `jobs.logs`

### Phase C: Agent Guidance

- document when to use synchronous mode versus job mode
- teach long asset workflows to submit through the original tool, then poll via
  `jobs`

### Phase D: Scheduling Expansion

- only after MVP stability
- evaluate safe concurrency classes and optional queue controls

## Acceptance Criteria

This issue is done when:

1. `LOOMLE` has a shared job runtime plus a top-level `jobs` management tool
2. long Unreal tasks no longer depend on synchronous request deadlines for
   correctness
3. original tools can opt into `execution.mode = "job"` without inventing
   separate lifecycle protocols
4. callers can submit, inspect, and fetch results without guessing from logs
5. multiple jobs can exist at once even if first-version execution stays
   serial
6. session linkage is supported as metadata rather than as a hard dependency

## Related GitHub Issue

- [#132](https://github.com/loomle/loomle/issues/132) `execute timeout is a false negative: Unreal keeps running the task after EXECUTION_TIMEOUT`
