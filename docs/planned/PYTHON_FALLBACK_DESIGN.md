# Python Fallback Execution

## Status

The implementation is in progress on the Loomle 0.7 development branch. Its
public Client and Bridge source now include the eighth public tool, `python`,
but it is not a released contract until the packaged acceptance gates below
pass.

This document defines the eighth public tool, `python`. The tool has one
primary `run` operation and one continuation-only `poll` operation. It is not
part of a published Loomle release yet.

The design is confirmed. Client, Bridge, protocol, diagnostics, focused tests,
and release-documentation changes are present. Local arm64 `BuildPlugin`, the
complete 149-test UE Automation category, and exact-archive packaged acceptance
pass against both official UE 5.7 and UE 5.8 Launcher installations. The native
Windows release gates are still required before release.

The 2026-08-09 safe-entry audit moved Python admission from a Game Thread
TaskGraph task to the persistent Core Ticker described below. Focused tests now
prove that a TaskGraph entry is deferred and that synchronous
`AssetTools.ImportAssetTasks` imports and cleans up a PNG texture without
recursively processing the named-thread queue. These tests pass on the official
UE 5.7 and UE 5.8 Launcher installations for Mac arm64. The original reported
environment was Windows UE 5.7.4, so that platform remains an explicit release
gate rather than an inferred result from the Mac validation.

The 2026-08-09 PIE fallback audit removed the earlier blanket play-session
rejection. Focused admission automation passes on the official UE 5.7 and UE
5.8 Launcher installations for Mac arm64. A live public-MCP acceptance on UE
5.7 requested PIE from one short Python call, observed one valid Game World and
advancing gameplay time in later calls, requested stop, and confirmed that the
Game World was gone. The resident `debug-unreal-pie-with-python` Skill now owns
the permission, short-call, world-reacquisition, and cleanup workflow; Loomle
does not own a parallel PIE lifecycle state machine.

The 2026-08-09 Agent-guidance audit added the resident
`use-unreal-python` safety workflow and narrowed
`debug-unreal-pie-with-python` to an additive PIE specialization. A focused
partial-failure recovery test now proves that a failed Python execution can
leave an asset applied, reports `stateMayHaveChanged: true`, and can be
followed by an idempotent recovery call that reuses rather than duplicates the
asset. The test compiles and passes on the official UE 5.7 and UE 5.8 Launcher
installations for Mac arm64. Client, Skill-validation, site-build, and Fab
packaging tests also pass for the unified guidance chain.

## Decision

`python` is Loomle's high-privilege escape hatch for Unreal Editor behavior
that is already well served by UE Python but is not yet expressed through a
structured Loomle interface.

It runs full embedded Unreal Python, including `import unreal`. It is not a
second SAL orchestration language, and it does not reproduce UE 5.8's
restricted Programmatic Toolset. An agent uses SAL when Loomle already exposes
the required semantics and uses Python when the structured interface boundary
does not cover the necessary native workflow.

The public contract is deliberately small:

```text
python(operation: "run" | "poll")
```

- `run` supplies inline source that defines `run()` and normally returns its
  structured result directly.
- `poll` exists only when an earlier `run` exceeded Loomle's short inline
  completion window and returned an `executionId` plus an exact continuation.

There is no caller-provided purpose or reason string. Natural-language intent
is not machine-verifiable, does not constrain what the script can do, and
would duplicate information already available to the agent and reviewing
user. The source and its actual effects remain authoritative.

## Agent Guidance Layering

The tool contract and resident Agent Skills form one progressive guidance
chain rather than three parallel sources of workflow policy:

```text
python tool description
-> use-unreal-python
-> debug-unreal-pie-with-python when the task enters PIE
```

The public `python` description owns only permanent capability facts and
routing: Python is a fallback, `run` is unrestricted and mutating, an exposed
continuation must be polled exactly, and the relevant resident Skills must be
loaded. It does not carry API-discovery, idempotency, recovery, persistence, or
PIE procedure.

`use-unreal-python` owns the general fallback workflow:

- live status and project-context checks;
- structured-interface selection before Python;
- live Unreal Python API and property discovery;
- small idempotent calls with stable target rediscovery;
- structured evidence, explicit persistence, and independent verification;
- exact continuation handling and conservative partial-state recovery.

`debug-unreal-pie-with-python` is an additive specialization. It owns only PIE
permission, session ownership, asynchronous start/stop requests, Game World
selection, cross-frame call boundaries, multi-world ambiguity, and cleanup. It
must direct the agent to load and follow `use-unreal-python` first instead of
duplicating its general safety policy.

This boundary lets the base policy evolve once while keeping PIE's distinct UE
lifecycle semantics discoverable only for runtime debugging tasks.

## Intent and Boundary

The fallback lets an agent complete a concrete UE Editor task without waiting
for Loomle to model every mature Python-supported domain.

Appropriate uses include:

- creating or bulk-populating a `UDataTable` through its existing RowStruct;
- creating and editing an instance of an existing `UDataAsset` subclass;
- using a project or third-party plugin's documented Unreal Python API;
- invoking an Editor subsystem or native operation that has no SAL surface;
- performing project-specific batch repair or migration through UE's object
  model.

It should not be used:

- to reimplement ordinary SAL query or patch composition;
- as an automatic retry after a structured operation fails;
- to bypass a validation error returned by the owning Loomle interface;
- to claim dry-run, rollback, atomicity, idempotency, or safe cancellation;
- as agent-local Python. It executes inside the bound Unreal Editor process.

Repeated use in one stable workflow is product evidence for a structured
Loomle interface only when that interface would materially add native
identities, validation, dry-run, diff, revision, or Editor-specific diagnostic
semantics. Python remains a permanent complement for mature open-ended UE
domains rather than a temporary implementation defect that SAL must absorb in
full.

## UE Source Basis

The implementation target is UE 5.7. Relevant source is:

- `Engine/Plugins/Experimental/PythonScriptPlugin/PythonScriptPlugin.uplugin`
- `Engine/Plugins/Experimental/PythonScriptPlugin/Source/PythonScriptPlugin/Public/IPythonScriptPlugin.h`
- `Engine/Plugins/Experimental/PythonScriptPlugin/Source/PythonScriptPlugin/Public/PythonScriptTypes.h`
- `Engine/Plugins/Experimental/PythonScriptPlugin/Source/PythonScriptPlugin/Private/PythonScriptPlugin.cpp`

`IPythonScriptPlugin` is the supported native boundary. It reports whether
Python is configured, available, and initialized; it can request runtime
enablement; and it executes an `FPythonCommandEx`.

`FPythonCommandEx` supplies several useful native behaviors:

- `ExecuteFile` runs multiline source or a `.py` file;
- `Private` file scope provides a fresh locals/globals dictionary based on
  UE's default Python globals;
- `Unattended` suppresses some interactive UI but cannot guarantee that an
  invoked API will never display or wait for UI;
- `LogOutput` captures ordered Python `Info`, `Warning`, and `Error` entries;
- failure places a Python exception trace in `CommandResult`.

`Private` scope is namespace isolation, not a security sandbox. Imported
modules, UObject state, packages, files, processes, network activity, module
caches, and other process-global state remain reachable. UE evaluates the
script inside the Editor process while holding the embedded interpreter's GIL.

UE 5.8's Programmatic Toolset is useful precedent for a different goal. It
runs agent-authored Python in the same embedded interpreter but restricts
imports and builtins, prevents direct `unreal` access, and injects an
`execute_tool` function that marshals registered tools to the Editor thread.
That design safely optimizes structured tool orchestration. Loomle does not
adopt its capability restriction because this fallback exists specifically to
reach UE APIs that have no registered structured tool.

Loomle does adopt three useful principles from that design:

- one named `run()` entry point;
- an explicitly JSON-compatible returned dictionary;
- asynchronous request completion must not imply that UE work is safely
  cancellable.

## Public MCP Tool

### Name and annotations

The public tool is named `python`. Its annotations are conservative because
MCP annotations apply to the whole tool rather than to an individual
operation:

```json
{
  "readOnlyHint": false,
  "destructiveHint": true,
  "idempotentHint": false,
  "openWorldHint": true
}
```

`poll` is read-only, but the combined tool must retain the destructive and
open-world classification required by `run`.

The permanent description should be concise and route the agent to resident
workflow guidance:

> Run full Python in the bound Unreal Editor when no structured Loomle
> interface covers the capability. Before run, load `use-unreal-python`; for
> PIE also load `debug-unreal-pie-with-python`. Follow a returned `poll`
> exactly; never replay. No built-in dry run, rollback, safe cancellation, or
> idempotency.

### Input schema

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "oneOf": [
    {
      "properties": {
        "operation": { "const": "run" },
        "script": {
          "type": "string",
          "minLength": 1,
          "maxLength": 262144,
          "description": "Inline Unreal Editor Python defining one synchronous run() entry point."
        }
      },
      "required": ["operation", "script"],
      "additionalProperties": false
    },
    {
      "properties": {
        "operation": { "const": "poll" },
        "executionId": {
          "type": "string",
          "minLength": 1,
          "description": "Opaque handle returned by an earlier running result."
        }
      },
      "required": ["operation", "executionId"],
      "additionalProperties": false
    }
  ]
}
```

`operation` is required. Explicit `run` prevents a malformed status-like call
from being interpreted as executable source, while explicit `poll` makes the
continuation self-describing.

The first version has no `purpose`, `reason`, `mode`, `file`, `args`, `cwd`,
`environment`, `dryRun`, `timeout`, `transaction`, `expectedRevision`, result
schema, cancellation, or caller-selected execution id.

The caller never supplies a source-file path. Loomle owns all temporary paths.

## Script Contract

A script defines one synchronous, no-argument `run()`:

```python
import unreal

def run():
    asset = unreal.load_asset("/Game/Data/DA_Weapon")
    asset.set_editor_property("damage", 25.0)
    return {
        "assetPath": asset.get_path_name(),
        "changedProperties": ["damage"],
        "saved": False,
    }
```

The complete source is executed in a fresh per-call namespace and then Loomle
calls `run()` from that namespace. Top-level imports and statements are part of
the execution and may have side effects. The fresh namespace does not isolate
imported modules or other process-global state.

The entry point must be callable, synchronous, and accept no arguments.
`async def`, generators, and a callable requiring arguments are invalid.

`run()` must return a top-level `dict`. Its complete value must recursively
contain only:

- `None`;
- Boolean values;
- integers in JavaScript's exactly representable safe range
  `[-(2^53-1), 2^53-1]` and finite floating-point numbers;
- strings;
- lists of compatible values;
- dictionaries with string keys and compatible values.

Cycles, integers outside the safe range, tuples, sets, bytes, NaN, infinity,
UObject wrappers, reflected structs, and other Python values are rejected.
The safe-integer rule prevents Python's arbitrary-precision integers from
silently changing value when they cross MCP JSON and the TypeScript Client.
Loomle does not guess how to serialize a UE object. The script must project it
into stable, useful facts such as an object path, class path, GUID, name, or
ordinary properties.

For example, this is invalid:

```python
return {"asset": asset}
```

This is valid:

```python
return {
    "assetPath": asset.get_path_name(),
    "classPath": asset.get_class().get_path_name(),
}
```

The agent defines the domain-specific result shape in its own code. Loomle
validates only the stable outer contract and JSON compatibility; it does not
require a duplicate caller-provided result schema.

## Result Model

The canonical MCP payload is `structuredContent`. `content` contains a concise
text/JSON mirror for hosts that do not expose structured results. Logs never
replace the returned object.

The public output schema is:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["status", "stateMayHaveChanged"],
  "properties": {
    "status": {
      "type": "string",
      "enum": ["running", "succeeded", "failed", "lost"]
    },
    "executionId": { "type": "string", "minLength": 1 },
    "stateMayHaveChanged": { "type": "boolean" },
    "result": {
      "type": "object",
      "additionalProperties": true
    },
    "error": {
      "type": "object",
      "required": ["code", "phase", "message", "retryable"],
      "properties": {
        "code": { "type": "string", "minLength": 1 },
        "phase": {
          "type": "string",
          "enum": ["validation", "staging", "execution", "result", "runtime"]
        },
        "message": { "type": "string", "minLength": 1 },
        "traceback": { "type": "string" },
        "retryable": { "type": "boolean" }
      },
      "additionalProperties": false
    },
    "logs": {
      "type": "array",
      "maxItems": 1000,
      "items": {
        "type": "object",
        "required": ["type", "output"],
        "properties": {
          "type": {
            "type": "string",
            "enum": ["info", "warning", "error"]
          },
          "output": { "type": "string" }
        },
        "additionalProperties": false
      }
    },
    "logsTruncated": { "type": "boolean" },
    "durationMs": { "type": "integer", "minimum": 0 },
    "elapsedMs": { "type": "integer", "minimum": 0 },
    "continuation": {
      "type": "object",
      "required": ["tool", "arguments", "pollAfterMs"],
      "properties": {
        "tool": { "const": "python" },
        "arguments": {
          "type": "object",
          "required": ["operation", "executionId"],
          "properties": {
            "operation": { "const": "poll" },
            "executionId": { "type": "string", "minLength": 1 }
          },
          "additionalProperties": false
        },
        "pollAfterMs": { "type": "integer", "minimum": 0 }
      },
      "additionalProperties": false
    }
  },
  "additionalProperties": false
}
```

The status-specific invariants are normative:

- `running` requires `executionId`, `elapsedMs`, and `continuation`; it has no
  `result`, `error`, `logs`, or `durationMs`;
- `succeeded` requires `result`, `logs`, `logsTruncated`, and `durationMs`; it
  has no `error`, `elapsedMs`, or continuation;
- `failed` requires `error`; an executed failure also returns terminal logs and
  duration, while a pre-execution failure may omit them;
- `lost` requires `executionId` and `error` and has no result;
- terminal results returned through `poll` include `executionId`; fast terminal
  `run` results do not.

The MCP tool result sets `isError=true` for `failed` and `lost`, but their
structured payload remains available. `running` and `succeeded` do not set
`isError`.

### Fast success

Most scripts should finish inside the inline completion window and return
directly:

```json
{
  "status": "succeeded",
  "stateMayHaveChanged": true,
  "result": {
    "assetPath": "/Game/Data/DT_Weapons.DT_Weapons",
    "rowsCreated": 42,
    "saved": true
  },
  "logs": [],
  "logsTruncated": false,
  "durationMs": 184
}
```

A fast terminal result does not expose an `executionId`. Loomle may use an
internal record while the call is running, but normal agent workflows do not
need to see a job abstraction.

### Running continuation

If an admitted script is still executing after Loomle's short inline
completion window, `run` returns a non-error running result:

```json
{
  "status": "running",
  "executionId": "py_01K...",
  "stateMayHaveChanged": true,
  "elapsedMs": 1007,
  "continuation": {
    "tool": "python",
    "arguments": {
      "operation": "poll",
      "executionId": "py_01K..."
    },
    "pollAfterMs": 1000
  }
}
```

The MCP text mirror must explicitly say that execution is still running, the
script must not be replayed, and the exact continuation should be called.

This outcome is not a timeout and does not set MCP `isError`. It means only
that the request has detached from an execution that already started.

### Poll while running

The agent calls `poll` only after receiving a continuation:

```json
{
  "operation": "poll",
  "executionId": "py_01K..."
}
```

If execution is not terminal, `poll` returns the same running shape with an
updated `elapsedMs` and continuation. `poll` is a snapshot read and returns
quickly; it does not wait for another long interval.

### Polled success

When the detached execution completes successfully, `poll` returns:

```json
{
  "status": "succeeded",
  "executionId": "py_01K...",
  "stateMayHaveChanged": true,
  "result": {
    "assetPath": "/Game/Data/DT_Weapons.DT_Weapons",
    "rowsCreated": 42,
    "saved": true
  },
  "logs": [],
  "logsTruncated": false,
  "durationMs": 2841
}
```

The terminal `poll` result retains `executionId` so the response remains
self-identifying. Repeating the same `poll` during its retention period returns
the same terminal outcome and does not re-execute Python.

### Executed failure

A Python exception or invalid returned value after execution starts produces:

```json
{
  "status": "failed",
  "stateMayHaveChanged": true,
  "error": {
    "code": "runtime.python_execution_failed",
    "phase": "execution",
    "message": "Object has no editor property named damage",
    "traceback": "Traceback (most recent call last): ...",
    "retryable": false
  },
  "logs": [],
  "logsTruncated": false,
  "durationMs": 23
}
```

A detached failure also contains its `executionId`. Executed failures set MCP
`isError=true` without discarding `structuredContent`.

`stateMayHaveChanged` is conservative:

- validation or staging failure before the script begins uses `false`;
- top-level execution, `run()`, serialization failure after `run()`, and all
  uncertain detached outcomes use `true`;
- successful execution uses `true` because Loomle cannot prove that arbitrary
  Python was read-only.

No failure with `stateMayHaveChanged=true` may invite an automatic retry.

### Lost execution

If the owning Editor exits, crashes, or is replaced before Loomle observes a
terminal outcome, `poll` returns an error result:

```json
{
  "status": "lost",
  "executionId": "py_01K...",
  "stateMayHaveChanged": true,
  "error": {
    "code": "runtime.python_execution_lost",
    "phase": "runtime",
    "message": "The Editor runtime that owned this execution is no longer available.",
    "retryable": false
  }
}
```

`lost` does not mean rollback and does not prove whether the script completed.

### Output bounds

The structured result is bounded to 1 MiB of UTF-8 encoded JSON. It is never
silently truncated because truncation would change the agent-defined shape. An
oversized result fails with `runtime.python_result_too_large` after execution
and therefore reports `stateMayHaveChanged=true`.

Terminal log output preserves UE's ordered `info`, `warning`, and `error`
entries. It is bounded to 1,000 entries and 256 KiB of combined UTF-8 text.
Deterministic head-and-tail truncation sets `logsTruncated=true` and does not
change execution success.

The native `FPythonCommandEx` log array becomes available when its synchronous
call finishes. The first version therefore returns logs with terminal results;
`poll` is not a live log-streaming API. Captured Python output is not a complete
copy of UE's global Output Log, and asynchronous output after `run()` returns
is outside the execution result.

## Execution Lifecycle

### Admission and fast completion

`run` has two distinct timing boundaries:

1. **Game Thread admission budget.** The pending execution must atomically
   enter `started` from Loomle's persistent Core Ticker callback within the
   existing short admission budget. If it does not, Loomle cancels it before
   execution and returns `runtime.editor_unresponsive`.
2. **Inline completion window.** After `started`, Loomle waits only a short
   internal interval, initially approximately one second, for a terminal
   result. If the script is still running, Loomle returns its continuation.

The inline completion window is not an execution timeout, not a user option,
and not a service-level promise about task duration. Its only purpose is to
keep the common fast path to one MCP call while ensuring an unexpectedly slow
script promptly yields control back to the agent.

The boundary is race-safe: the execution record has one synchronized terminal
transition. Loomle returns either the observed terminal result or a handle to
that same continuing execution, never both independent outcomes.

### Safe Game Thread entry

Running on the Game Thread is necessary but not sufficient for unrestricted
Unreal Python. Loomle must also enter Python from a UE call stack that permits
synchronous engine APIs to pump Game Thread work.

In particular, Loomle must never start Python from
`AsyncTask(ENamedThreads::GameThread, ...)`, a TaskGraph callback, package or
asset loading, package saving, or garbage collection. Interchange synchronous
waits may process the Game Thread TaskGraph queue. Starting Python from that
same queue and then importing an asset can recursively process the named-thread
queue and trigger UE's TaskGraph recursion guard.

The Bridge therefore owns one persistent zero-delay
`FTSTicker::GetCoreTicker()` callback. `python.run` stages one execution in a
thread-safe pending slot, and only that callback may atomically admit and call
`ExecPythonCommandEx`. This applies even when the submitting caller already
runs on the Game Thread: thread identity does not prove that its call stack is
safe. UE's Interchange task system explicitly identifies an Engine tick and the
Core Ticker as safe contexts for synchronous waits.

The pending slot is not a general queue. It belongs to the one active execution
record and is cleared when the ticker claims it, admission is cancelled, or the
Editor begins shutdown.

### Detached does not mean background UE execution

The Python call itself remains on the Game Thread because the script has full
direct `unreal` access. Returning an `executionId` detaches the MCP request; it
does not move arbitrary UObject work onto a safe worker thread.

A long or blocked script can therefore freeze Editor UI and prevent other
UE-backed Loomle operations from entering the Game Thread. This limitation is
fundamental to unrestricted Unreal Python. Loomle must not describe detached
execution as safe parallelism.

### Poll path

`poll` reads a thread-safe execution record on the Bridge listener/worker path.
It must not dispatch to the Game Thread and must remain callable while the Game
Thread is occupied by the script.

The Client binds the returned opaque execution handle to the exact runtime
that produced it. Polling must target that runtime rather than re-resolving the
project to a replacement Editor. A changed or vanished runtime produces
`lost`, never a lookup against another Editor.

An exposed execution remains available while running. After it becomes
terminal, the Bridge retains the bounded result for at least 30 minutes while
the same Editor runtime remains alive. The implementation may discard the
record sooner only after a terminal result has been successfully returned and
the documented replay grace period has elapsed. Polling an expired handle
returns `runtime.python_execution_expired` and never re-runs the script.

### Concurrency

One Editor runtime admits at most one Python fallback execution at a time. A
second `run` while one is active fails before execution with
`runtime.python_busy` and returns the active `executionId` only if that id was
already exposed.

Loomle does not queue a hidden sequence of Python scripts. A queued script
could otherwise begin mutating UE after the requesting agent had moved on.

## Private Bridge Protocol

The one public tool maps to two private operations:

```text
python.run
python.poll
```

Private separation is required because their execution paths differ:

- `python.run` performs project/runtime preflight, stages source, enters Game
  Thread admission from the persistent Core Ticker, and starts the execution;
- `python.poll` performs an exact-runtime, non-Game-Thread record lookup.

Both use the existing JSON-RPC transport. Implementation increments the
current Client-Bridge protocol version and advertises both operations through
`rpc.capabilities`.

The `executionId` is opaque and unique to one Editor runtime. It is not an
idempotency key, caller-selected request id, permission token, or durable
cross-restart job identity.

## Python Runner and Result Transport

UE's `ExecuteFile` mode normally returns `None` in `CommandResult`; it does not
directly return the Python value produced by an arbitrary `run()` function.
Loomle therefore owns a small staged runner protocol rather than treating
native log output as the result.

For each execution, the Bridge creates unique files below a Loomle-owned
project `Saved` directory:

- the agent source file;
- a Loomle runner file;
- a result JSON path.

The runner executes the source in a fresh dictionary, validates and calls
`run()`, recursively validates its returned value, and serializes it with
strict JSON rules including finite numbers. The Bridge invokes the runner
through `FPythonCommandEx` using:

```text
ExecutionMode = ExecuteFile
FileExecutionScope = Private
Flags = Unattended
```

The runner writes only its result transport document to the result path. The
Bridge reads and independently validates that document, combines it with
native `CommandResult` and `LogOutput`, publishes the terminal execution
record, and removes all staging files on ordinary success and failure.

The source path gives Python tracebacks a useful filename. Paths containing
spaces must be correctly quoted for UE's native `.py` command parser.

This staging protocol is not a sandbox. The executed source has full process
permissions and can inspect, modify, move, or delete staging files. Cleanup is
best effort, and a process crash may leave files behind. Agents must not place
credentials or other secrets in scripts. Loomle never echoes or deliberately
logs the complete source, although a Python traceback may contain relevant
source lines.

The service must live in a focused Python execution component. It must not
restore Loomle 0.6's direct-tool runtime, monkey-patch `unreal`, install
process-global signal handlers, or depend on shared console globals.

## Python Availability and Editor State

The Bridge declares `PythonScriptPlugin` as a plugin/module dependency and
requests Python enablement during Editor initialization, outside an agent
execution. It tracks `OnPythonInitialized` and `OnPythonShutdown` and verifies
`IsPythonConfigured`, `IsPythonAvailable`, and `IsPythonInitialized` before
admission.

A tool call never busy-waits for interpreter initialization on the Game
Thread. Not ready and unavailable states return distinct errors.

`run` remains available while PIE is active. Loomle does not choose an Editor
World or Play World and does not own a second PIE lifecycle state machine. The
script must explicitly obtain the world required by the task through UE's
native Python APIs.

PIE start and stop requests are asynchronous Game Thread state transitions.
One Python execution can submit a request through `LevelEditorSubsystem`, but
it must return before UE can advance that request on later Editor ticks. An
agent therefore controls PIE with multiple short `python.run` calls:

1. inspect the current play state and request start when necessary;
2. return immediately, then use a new `python.run` call to confirm that PIE is
   active and `UnrealEditorSubsystem.get_game_world()` returns a world;
3. run short observation or mutation scripts, reacquiring the world and every
   UObject on each call;
4. request end play in a separate call and later confirm that PIE stopped.

`python.poll` is only the continuation for one already-running Python
execution. It must not be used to wait for PIE to start, stop, or advance a
frame. A Python script must not sleep or busy-wait for PIE state because it
occupies the Game Thread and prevents the transition or gameplay tick it is
waiting for.

Starting PIE changes the user's active Editor session. Agent workflow guidance
must ask for permission before requesting start unless the user's current
instruction already explicitly authorizes running or debugging PIE. The agent
should normally stop a session it started after completing the requested
debugging, unless the user asks to leave it running.

## Dry Run, Transactions, and Side Effects

Full Python cannot implement Loomle's mutation dry-run contract. Source cannot
be reliably reduced to parse, resolve, validate, and plan phases before it
runs. Runtime reflection and ordinary control flow can choose operations
dynamically.

Effects that an Editor transaction cannot generally reverse include:

- saving or deleting packages and files;
- changing config, console variables, subsystem, or process-global state;
- spawning processes or making network requests;
- invoking UE APIs that do not call `Modify`;
- starting asynchronous work that outlives `run()`.

The Bridge does not wrap arbitrary code in a Loomle-owned transaction.
`FScopedTransaction::Cancel()` would remove a transaction record but would not
generally restore effects already applied. The result never reports SAL
mutation fields such as `valid`, `planned`, `applied`, `diff`, or revisions.

The script may choose a UE transaction or explicit save operation when that is
correct for its specific domain, but Loomle makes no generic guarantee about
undoability, persistence, or rollback.

## Cancellation, Failure, and Replay

There is no safe generic way to preempt unrestricted Python while it may be
executing native UE code on the Game Thread. Injecting an exception,
terminating a thread, or shutting down the interpreter can corrupt Editor
state.

Consequently:

- `python` has no cancel operation;
- the inline completion window never attempts to stop execution;
- `poll` observes but does not control the execution;
- caller abort after Game Thread admission abandons only the wait;
- the Client and Bridge never automatically retry or replay a script;
- Editor termination may be the only recovery from an infinite loop, with
  possible loss of unsaved work.

The normal continuation path prevents a slow script from reaching the outer
transport timeout. It cannot eliminate every uncertain outcome. A connection
loss, MCP host cancellation, Client crash, or Editor crash before the running
continuation is delivered may leave the agent without an `executionId` even
though Python started. That case reports outcome uncertainty when possible and
must never invite blind replay.

Admission cancellation remains safe only before the task reaches `started`.
After `started`, every failure is treated as potentially mutating.

## Errors

The stable planned errors are:

| Code | Meaning | Retry guidance |
| --- | --- | --- |
| `tool.invalid_arguments` | invalid operation, missing script/id, oversized input, or unknown field | fix the request |
| `runtime.python_unavailable` | target build or plugin has no usable Python | do not retry unchanged |
| `runtime.python_initializing` | Python was requested but is not ready | retry after readiness changes |
| `runtime.python_source_staging_failed` | staging failed before Python began | repair filesystem state |
| `runtime.python_busy` | another fallback execution is active | follow its exposed continuation or wait |
| `runtime.python_execution_failed` | source, entry point, or Python/UE execution failed | inspect traceback and current state |
| `runtime.python_invalid_result` | `run()` did not return a compatible dictionary | inspect current state; fix source |
| `runtime.python_result_too_large` | the structured result exceeded its bound | inspect current state; return a smaller projection |
| `runtime.python_execution_not_found` | id is unknown to the owning live runtime | verify the exact continuation |
| `runtime.python_execution_expired` | retained terminal result has expired | do not replay automatically |
| `runtime.python_execution_lost` | the owning runtime vanished before a terminal outcome was observed | inspect project state; do not replay automatically |
| `runtime.editor_unresponsive` | Game Thread did not admit the task | retry only after Editor responsiveness returns |
| `runtime.editor_shutting_down` | Editor is draining | retry after restart and state inspection |

Project binding, multiple-Editor, startup, protocol, and transport errors retain
their existing codes. Python-specific formatting must override any generic
“retryable timeout” suggestion after execution may have started.

## Implementation Scope

The first implementation changes:

- the Client public tool definition, routing, validation, structured result
  formatting, and tests;
- Bridge capabilities and the `python.run`/`python.poll` RPC paths;
- a focused staged Python runner and thread-safe execution-record service;
- plugin/module dependency declarations and initialization tracking;
- the generated private protocol version;
- diagnostic catalog entries;
- runtime liveness behavior so `poll` bypasses Game Thread readiness while
  preserving exact runtime identity;
- current tool-count, dry-run-policy, coverage, packaging, and release-test
  documentation.

It does not add:

- `exec` or `eval` public modes;
- arbitrary caller-provided script paths;
- caller-provided purpose, schema, timeout, or execution id;
- cancellation, priority, parallel execution, or a general job system;
- live log streaming;
- automatic UObject serialization;
- cross-Editor restart recovery;
- fake dry-run, transaction, or rollback claims;
- Python-based orchestration of SAL.

## Verification

### Client tests

Tests must prove:

- exactly eight public tools are listed and `python` has the conservative
  annotations above;
- the `run` and `poll` input branches reject missing, mixed, unknown, empty,
  and oversized fields;
- `run` uses the currently bound healthy runtime;
- `poll` follows the exact runtime bound to its returned execution id and does
  not jump to a replacement Editor;
- fast success and failure expose structured content without an execution id;
- running results expose the exact `python`/`poll` continuation and are not MCP
  errors;
- terminal polled failures retain error, logs, traceback, and
  `stateMayHaveChanged` detail;
- cancellation or transport failure never causes automatic replay.

### UE Automation

Native tests must cover:

- capabilities advertise `python.run` and `python.poll`;
- a script can import `unreal`, define `run()`, and return a nested JSON object;
- empty dictionaries, Unicode, lists, nulls, Booleans, and finite numbers
  round-trip exactly;
- non-string keys, cycles, out-of-range integers, UObject values, tuples, NaN,
  and infinity fail as invalid results;
- missing, parameterized, async, and generator `run()` definitions fail;
- syntax and runtime errors preserve useful tracebacks and prior native logs;
- source and runner paths containing spaces execute correctly;
- staging files are removed after ordinary success and failure;
- cleanup failure does not replace the execution outcome;
- the result size limit fails without producing malformed JSON;
- log truncation is deterministic and does not change success;
- Python submitted by an RPC worker starts from the Core Ticker rather than a
  TaskGraph Game Thread callback;
- synchronous `AssetTools.ImportAssetTasks` can import a transient image
  without recursively entering the Game Thread TaskGraph queue;
- Game Thread admission cancellation prevents later execution;
- a sub-window script returns directly without an exposed id;
- a script exceeding the inline window returns one id and later publishes the
  same terminal result;
- `poll` remains responsive from the worker path while the Game Thread script
  is active;
- repeated terminal polls do not re-execute the script;
- a second active `run` fails with `runtime.python_busy` and is never queued;
- Editor shutdown changes an unfinished exposed execution to `lost`;
- Python executes through the same safe Core Ticker entry while PIE is active;
- unavailable Python, initialization, and shutdown fail at their specified
  boundaries;
- no result claims dry run, rollback, idempotency, or safe cancellation.

Tests must use transient objects or copied fixtures and clean them explicitly.
Ordinary suites must not execute an infinite loop. Any destructive watchdog
test must own a disposable Editor process and its cleanup.

### Packaged end-to-end

The exact release archive must:

1. list the `python` tool with `run` and `poll` input branches;
2. execute a fast non-mutating script that imports `unreal` and returns a
   deterministic structured object;
3. surface one controlled Python exception with its traceback;
4. execute one deliberately delayed script, receive a continuation, and poll
   its final structured result;
5. prove the Editor remains responsive after all terminal executions;
6. leave no Loomle staging files after normal completion.

Release audit must also prove the Python plugin dependency is present and the
packaged Bridge still contains only the intended Loomle module set.

## Acceptance

The design is implemented only when:

- the public name, operations, schemas, annotations, results, and errors match
  this document;
- full `import unreal` execution works in the bound UE 5.7 Editor;
- `run()` returns a real structured object rather than using logs as data;
- the common fast path completes in one MCP call;
- a slower admitted execution promptly returns a usable `poll` continuation;
- polling stays independent of the blocked Game Thread and never changes
  runtime identity;
- code is never advertised as sandboxed, transactional, dry-runnable,
  safely cancellable, or automatically retryable;
- current documentation continues to prefer structured Loomle interfaces for
  the semantics they cover;
- focused Client tests, UE Automation, and packaged end-to-end validation pass
  on every supported platform.
