# LOOMLE Jobs Runtime Sync Handoff

Status:

- product-side implementation: complete
- test-side sync: complete

## Summary

`LOOMLE` now has a first-version long-running task runtime.

This is not a tool-specific `execute.jobs` feature.

The shipped model is:

- original tool submits work
- `execution.mode = "job"` requests long-task handling
- top-level `jobs` manages lifecycle

The first adopter is `execute`.

## What Shipped

### New Top-Level Tool

- `jobs`

Supported first-version actions:

- `status`
- `result`
- `logs`
- `list`

### New Submission Mode on `execute`

`execute` now accepts an optional `execution` object:

```json
{
  "execution": {
    "mode": "job",
    "idempotencyKey": "string",
    "label": "optional",
    "waitMs": 1000,
    "resultTtlMs": 3600000
  }
}
```

When `execution.mode = "job"`:

- `execute` no longer waits for final task completion
- it returns a `job` acceptance object
- final status, logs, and result move to `jobs`

## Current Runtime Contract

### `execute` in sync mode

Still behaves as before:

- normal synchronous result
- no `job` object required

### `execute` in job mode

Now returns:

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

### `jobs status`

Current payload shape includes:

- `jobId`
- `tool`
- `status`
- `acceptedAt`
- `startedAt`
- `finishedAt`
- `heartbeatAt`
- `resultAvailable`
- `message`
- `logCursor`

### `jobs result`

Current payload shape includes:

- `jobId`
- `tool`
- `status`
- `resultAvailable`
- `result`
- `stdout`
- `error`

### `jobs logs`

Current payload shape includes:

- `jobId`
- `entries`
- `nextCursor`
- `hasMore`

### `jobs list`

Current payload shape includes:

- `jobs[]`
  - `jobId`
  - `tool`
  - `label`
  - `status`
  - `acceptedAt`

## Important Product Boundaries

These are intentional first-version boundaries, not missing bug fixes:

1. only `execute` has opted into job mode
- `graph.verify` and `graph.mutate` have not opted in yet

2. the runner is serial-first
- multiple jobs may exist
- only one runs at a time in the current Unreal runtime

3. there is no `cancel` or `forget` yet
- do not write tests that assume control-plane cancellation

4. `jobs` is top-level
- do not expect `execute.jobs`
- do not expect `graph.jobs`

## Runtime Evidence Already Verified

The following product behavior has already been proven:

1. `execute` with `execution.mode = "job"` returns a stable `job` object
2. `jobs.status` transitions from `running` to `succeeded`
3. `jobs.logs` returns incremental log entries
4. `jobs.result` returns the final `execute` payload
5. idempotent re-submission with the same key reuses the same job
6. full `dev_verify` still passes after this change

## Test-Side Sync Coverage

### 1. Tool Inventory

Live tool-count expectations now include:

Current required tool count is now:

- `14`

The new required baseline tool is:

- `jobs`

### 2. Execute Semantics

Tests now distinguish:

- `execute` sync mode
- `execute` job mode

Do not keep assuming that all `execute` success paths return a final payload in
one call.

### 3. New Lifecycle Coverage

Current runtime coverage includes:

1. `execute` job submission returns `job.jobId`
2. `jobs.status` can observe `queued|running|succeeded|failed`
3. `jobs.result` returns non-terminal state without treating it as failure
4. `jobs.logs` returns ordered incremental log entries
5. `jobs.list` can recover outstanding work

### 4. Error Coverage

Current contract coverage includes:

1. missing `execution.idempotencyKey` in job mode
- expected: `IDEMPOTENCY_KEY_REQUIRED`

2. unknown `jobId`
- expected: `JOB_NOT_FOUND`

3. unsupported `jobs.action`
- expected: `JOB_ACTION_UNSUPPORTED`

### 5. Scope Discipline

Do not overreach in the first sync.

The test side should not yet assume:

- `cancel`
- `forget`
- concurrent job execution
- `graph.verify` job mode
- `graph.mutate` job mode

## Implemented Test Sync

1. smoke tool inventory now includes `jobs`
2. smoke includes one `execute(mode=job)` submission check plus basic `jobs` lifecycle reads
3. regression includes one end-to-end lifecycle check:
- submit
- poll status
- read logs
- read result
- confirm `jobs.list`
4. regression includes contract assertions for the three new error codes

## Suggested Manual Probe

```bash
/path/to/MyProject/Loomle/loomle \
  --project-root /path/to/MyProject \
  call execute \
  --args '{"mode":"exec","code":"import time, unreal\nunreal.log(\"jobs smoke start\")\ntime.sleep(2)\nunreal.log(\"jobs smoke end\")","execution":{"mode":"job","idempotencyKey":"manual-jobs-smoke","label":"manual_jobs_smoke","waitMs":1000}}'
```

Then query:

```bash
/path/to/MyProject/Loomle/loomle \
  --project-root /path/to/MyProject \
  call jobs \
  --args '{"action":"status","jobId":"<jobId>"}'
```

```bash
/path/to/MyProject/Loomle/loomle \
  --project-root /path/to/MyProject \
  call jobs \
  --args '{"action":"logs","jobId":"<jobId>","limit":200}'
```

```bash
/path/to/MyProject/Loomle/loomle \
  --project-root /path/to/MyProject \
  call jobs \
  --args '{"action":"result","jobId":"<jobId>"}'
```

## Important Boundary

This handoff does not ask the test side to invent a broader async platform.

It only asks them to sync to product behavior that already exists:

- top-level `jobs`
- `execute` job-mode submission
- serial-first lifecycle tracking for long Unreal tasks
