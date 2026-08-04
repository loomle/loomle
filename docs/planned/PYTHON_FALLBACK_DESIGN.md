# Python Fallback Execution

## Status

Loomle 0.7 does not currently expose Unreal-side Python. Its public Client has
seven tools, and its Bridge accepts only `sal.query`, `sal.patch`,
`editor.context`, `editor.open`, and `editor.close`.

This document defines a planned eighth public tool, `python_execute`, backed
by the private Bridge operation `python.execute`. It is a high-privilege
capability fallback for UE behavior that Loomle has not yet expressed through
SAL. It is not implemented and is not part of the current public contract.

## Decision

`python_execute` runs full Unreal Editor Python, including `import unreal`.
It does not reproduce UE 5.8's restricted Programmatic Toolset, whose Python
script runs in UE's existing embedded CPython interpreter but receives
restricted globals and can only combine already registered tools.

The distinction is intentional:

- the UE 5.8 Programmatic Toolset reduces round trips after an agent already
  understands the available tool schemas;
- Loomle's fallback reaches a native UE capability when no suitable structured
  Loomle interface exists yet;
- repeated fallback demand is evidence that Loomle should add or improve a
  structured interface, not a reason to make Python the normal workflow.

The Client and Bridge never fall back automatically. A failed SAL request is
not itself permission to run Python: invalid syntax, stale identity, invalid
arguments, unavailable Editor state, and implementation bugs should be
diagnosed through their owning interface. The agent selects
`python_execute` only after identifying a concrete capability gap.

## Intent

The fallback should let an agent make forward progress when UE exposes a
necessary Editor operation through Python but the active Loomle interface
catalog does not.

Appropriate uses include:

- inspecting an Editor subsystem that has no SAL Query surface;
- invoking a UE-native editor operation that has no SAL Patch operation;
- prototyping the smallest native workflow needed to understand a capability
  before designing its structured interface;
- repairing a project through a UE Python API while preserving UE's own object
  model and implementation path.

It should not be used:

- to batch or branch over operations already covered by SAL;
- as an automatic retry after a structured operation fails;
- to bypass a native validation error returned by SAL;
- to claim dry-run, atomicity, rollback, idempotency, or safe cancellation;
- as agent-local Python. It executes inside the bound Unreal Editor process.

## UE Source Basis

The implementation target is UE 5.7. Relevant engine source is:

- `Engine/Plugins/Experimental/PythonScriptPlugin/PythonScriptPlugin.uplugin`
- `Engine/Plugins/Experimental/PythonScriptPlugin/Source/PythonScriptPlugin/Public/IPythonScriptPlugin.h`
- `Engine/Plugins/Experimental/PythonScriptPlugin/Source/PythonScriptPlugin/Public/PythonScriptTypes.h`
- `Engine/Plugins/Experimental/PythonScriptPlugin/Source/PythonScriptPlugin/Private/PythonScriptPlugin.cpp`

`IPythonScriptPlugin` is the native boundary. It reports configuration and
initialization state, can force initialization at runtime, and executes an
`FPythonCommandEx`.

`FPythonCommandEx` supplies the behavior Loomle needs:

- `ExecuteFile` runs a multiline script or a script file;
- `EvaluateStatement` evaluates one expression and returns its Python
  representation in `CommandResult`;
- `Private` file scope gives a script a copied globals dictionary rather than
  the shared console globals;
- `Unattended` asks UE to suppress certain pieces of interactive UI, but does
  not guarantee that the script or an invoked UE API cannot display or wait
  for UI;
- `LogOutput` captures ordered `Info`, `Warning`, and `Error` entries;
- a failed command returns a Python exception trace in `CommandResult`.

Private file scope is namespace isolation, not a security sandbox. Imported
modules, UE objects, packages, files, processes, network activity, and other
process-global state remain reachable. The Python plugin executes code in the
Editor process and holds the Python GIL while evaluating it.

UE 5.8 provides useful precedent but a different boundary in
`Engine/Plugins/Experimental/Toolsets/EditorToolset/Content/Python/editor_toolset/toolsets/programmatic.py`.
Its `ProgrammaticToolset.execute_tool_script` does not start a separate Python
process or subinterpreter. The toolset module itself imports `unreal`, then
runs the agent script on a worker thread with `exec` and a custom globals
dictionary in the same embedded interpreter. AST validation, a replacement
`__import__`, a reduced builtins dictionary, and a read-only project-file
opener constrain direct `unreal` imports and ordinary host access from the
agent script. The injected `execute_tool` function marshals registered tool
calls back to the Editor event loop and exchanges JSON-compatible dictionaries.

That is an orchestration restriction layer, not process isolation. Loomle
adopts the useful temporary-file and explicit-result lessons where applicable,
but not the restricted capability model: this fallback exists specifically
for UE capabilities that have no registered structured tool.

## Topology

```text
MCP agent
  -> python_execute
      -> TypeScript Client project/runtime preflight
          -> rpc.invoke(tool="python.execute")
              -> Bridge Game Thread admission
                  -> IPythonScriptPlugin::ExecPythonCommandEx
                      -> UE Python API and Editor state
```

The public tool uses readable snake_case. The private transport operation keeps
the existing dotted naming convention.

## Public MCP Tool

### Name and annotations

The public name is `python_execute`.

Its MCP annotations are:

```json
{
  "readOnlyHint": false,
  "destructiveHint": true,
  "idempotentHint": false
}
```

Every call receives destructive annotations, including `eval`, because a
Python expression can invoke a mutating function.

The permanent description should be concise and explicit:

> Execute full Python inside the bound Unreal Editor only when no structured
> Loomle interface covers the required UE capability. This can create
> irreversible UE and external side effects and provides no dry run, rollback,
> cancellation, or idempotency guarantee. State the missing Loomle capability
> in `reason`.

### Input

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["reason", "code"],
  "properties": {
    "reason": {
      "type": "string",
      "minLength": 1,
      "maxLength": 512,
      "description": "Concrete Loomle capability gap that requires Unreal-side Python."
    },
    "mode": {
      "type": "string",
      "enum": ["exec", "eval"],
      "default": "exec"
    },
    "code": {
      "type": "string",
      "minLength": 1,
      "maxLength": 131072,
      "description": "Inline Python source. This is never interpreted as a caller-supplied script path."
    }
  },
  "additionalProperties": false
}
```

`language` is not an input because the tool name fixes the language.
`acknowledgeUnsafe` is not an input because a Boolean acknowledgement would be
checkbox theater rather than a safety boundary.

The first version intentionally has no `dryRun`, `timeout`, `file`, `args`,
`cwd`, `environment`, `transaction`, `expectedRevision`, or background-job
field. Adding any of those requires a separate design backed by real behavior.

`reason` is operational evidence for interface demand. It must describe the
missing capability rather than merely say that Python is easier. It is not
injected into the Python namespace, and it is telemetry rather than user
consent or a security boundary.

### Execution modes

`exec` executes multiline inline source. The Bridge writes the supplied source
to a unique Loomle-owned temporary `.py` file below the project's `Saved`
directory, invokes it with:

```text
ExecutionMode = ExecuteFile
FileExecutionScope = Private
Flags = Unattended
```

and removes the file on both success and ordinary failure. The caller cannot
select a file path. A process crash can leave the temporary source behind, so
callers must not place credentials or other secrets in fallback code.

`eval` evaluates exactly one Python expression with
`EvaluateStatement | Unattended` and returns UE's native Python
representation. It is not a read-only mode. UE's native expression evaluator
uses the interpreter's console context, so scripts must not depend on clean or
persistent cross-call global state.

Both modes have full process privileges. Private file scope does not restrict
imports, filesystem access, subprocesses, network access, UE reflection, or
mutation.

### Result

The public MCP result contains one text item holding a JSON object. JSON is
used here because Python output is not SAL Object Text and must preserve native
log entry boundaries without inventing another textual grammar.

Successful `exec` example:

```json
{
  "success": true,
  "mode": "exec",
  "result": "None",
  "logs": [
    {
      "type": "info",
      "output": "updated 3 actors"
    }
  ],
  "durationMs": 24,
  "resultTruncated": false,
  "logsTruncated": false,
  "temporarySourceRetained": false
}
```

Successful `eval` example:

```json
{
  "success": true,
  "mode": "eval",
  "result": "3",
  "logs": [],
  "durationMs": 1,
  "resultTruncated": false,
  "logsTruncated": false,
  "temporarySourceRetained": false
}
```

The successful-or-executed-failure payload schema is:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": [
    "success",
    "mode",
    "result",
    "logs",
    "durationMs",
    "resultTruncated",
    "logsTruncated",
    "temporarySourceRetained"
  ],
  "properties": {
    "success": { "type": "boolean" },
    "code": {
      "type": "string",
      "enum": ["runtime.python_execution_failed"]
    },
    "message": {
      "type": "string",
      "description": "Failure summary; present only when success is false."
    },
    "mode": { "type": "string", "enum": ["exec", "eval"] },
    "result": { "type": "string" },
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
    "durationMs": { "type": "integer", "minimum": 0 },
    "resultTruncated": { "type": "boolean" },
    "logsTruncated": { "type": "boolean" },
    "temporarySourceRetained": { "type": "boolean" },
    "temporarySourcePath": { "type": "string", "minLength": 1 },
    "stateMayHaveChanged": { "type": "boolean" }
  },
  "allOf": [
    {
      "if": {
        "properties": { "success": { "const": false } },
        "required": ["success"]
      },
      "then": {
        "required": ["code", "message", "stateMayHaveChanged"],
        "properties": {
          "stateMayHaveChanged": { "const": true }
        }
      },
      "else": {
        "not": {
          "anyOf": [
            { "required": ["code"] },
            { "required": ["message"] },
            { "required": ["stateMayHaveChanged"] }
          ]
        }
      }
    },
    {
      "if": {
        "properties": { "temporarySourceRetained": { "const": true } },
        "required": ["temporarySourceRetained"]
      },
      "then": {
        "required": ["temporarySourcePath"]
      }
    }
  ],
  "additionalProperties": false
}
```

`result` is always a string because `FPythonCommandEx::CommandResult` is a
native string representation, not guaranteed JSON. For `exec` it is normally
`None`; scripts should use `print` or `unreal.log*` for useful output. Native
log types map to public snake_case values `info`, `warning`, and `error` while
preserving order. An `error` log entry does not by itself mean execution
failed; `success` follows the Boolean returned by `ExecPythonCommandEx`.

The captured entries cover Python stdout, stderr, and `unreal.log*` activity
observed by UE's Python log capture during the synchronous call. They are not a
complete copy of the UE Output Log. Output emitted asynchronously or after the
tool returns is outside the result, and other Python activity may interleave
with the global capture window.

Result and log text must be bounded before returning to the Client. Truncation
is deterministic, retains diagnostically useful head and tail text, and sets
the corresponding flag. The implementation constants and tests must agree;
the initial maximums are 64 KiB of UTF-8 for `result`, 1,000 log entries, and
256 KiB of combined UTF-8 log output. Truncation preserves valid Unicode and is
not an execution failure.
UE builds the complete `CommandResult` and appends native `LogOutput` entries
before Loomle can truncate them. These limits protect the RPC response and
agent context only; they do not provide execution-time memory isolation.
Scripts that emit unbounded output can still exhaust Editor memory.

`temporarySourceRetained` is true when normal cleanup could not remove an
`exec` staging file. In that case the result also contains
`temporarySourcePath`; callers must treat the flag itself as a cleanup warning.
Cleanup failure does not replace the script's success or failure. A script has
full filesystem access and may move or alter its own source, so Loomle can only
report cleanup of the original staging path.

The tool never echoes `code`. Normal runtime diagnostics may include Python
traceback source lines returned by UE, so fallback code must still be treated
as non-secret.

## Private Bridge RPC

The private operation is:

```text
python.execute
```

It is invoked through the existing `rpc.invoke` envelope:

```json
{
  "protocolVersion": 5,
  "tool": "python.execute",
  "args": {
    "reason": "The current interfaces do not expose the Level Editor viewport FOV.",
    "mode": "exec",
    "code": "import unreal\nprint('ready')"
  }
}
```

The Bridge validates the same fields again. Client validation is an agent
usability boundary, not a trust boundary.

On success, `payload` is the result object described above. A syntax error or
an exception after Python starts is also a completed `rpc.invoke`: its payload
uses `success=false`, `code=runtime.python_execution_failed`, preserves the
mode, duration, native `CommandResult`, captured logs, and truncation facts,
and sets `stateMayHaveChanged=true`. The Client maps that payload to MCP
`isError=true` without discarding the native details.

Failures before Python starts use the existing JSON-RPC error envelope. This
distinction prevents an ordinary Python exception from being confused with
transport failure and preserves evidence of partial effects.

Adding a private operation changes the Client-Bridge contract. Implementation
therefore increments `loomle.protocolVersion` from `4` to `5`, regenerates both
Client and Bridge version sources, and adds `python.execute` to
`rpc.capabilities`. A version-4 Client and version-5 Bridge remain explicitly
incompatible even if their other tool names overlap.

## Runtime Preflight and Execution

The Client applies the same sticky project binding and live runtime selection
used by `sal_query`, `sal_patch`, and `editor`. The tool is unavailable when
the bound project is offline, starting, unresponsive, ambiguous, or
protocol-incompatible.

The initial implementation rejects execution while PIE or another play session
is active. The fallback targets Unreal Editor automation, and arbitrary Python
has no reliable generic rule for selecting Editor-world versus play-world
objects. The error is retryable after the user stops play.

The Bridge declares the Python plugin dependency and requests Python enablement
once during Editor initialization, outside a `python_execute` request. It
tracks `OnPythonInitialized` and `OnPythonShutdown` as capability state. A call
never busy-waits or performs slow interpreter initialization on the Game
Thread. `ForceEnablePythonAtRuntime` returning true means only that enablement
was requested; `IsPythonConfigured`, `IsPythonAvailable`, and
`IsPythonInitialized` remain the state authorities. A configured but not yet
fully initialized interpreter reports `runtime.python_initializing`; an
unavailable module, unsupported build, or initialization attempt that disabled
Python reports `runtime.python_unavailable`.

After Bridge Game Thread admission:

1. reject shutdown or play state;
2. acquire `IPythonScriptPlugin`;
3. verify the tracked capability state and `IsPythonInitialized`;
4. prepare the native command and temporary file, when required;
5. call `ExecPythonCommandEx` on the Game Thread;
6. capture duration, result, and ordered log output;
7. remove the temporary file with best effort;
8. return the result or structured failure.

The plugin descriptor enables `PythonScriptPlugin`, and the module build adds
`PythonScriptPlugin` as a private dependency. Runtime forcing during Editor
initialization is a defensive enablement step, not a replacement for that
declared dependency. Python initialization itself may display UE's native slow
task dialog; `Unattended` applies to command execution and is not a no-UI
guarantee.

For `exec`, the Bridge creates and normalizes a unique absolute staging path
and passes the correctly double-quoted path to `ExecuteFile`. UE identifies
files through `.py` command parsing, so paths containing spaces are a required
test case. Failure to write the staging file returns before Python begins.

The implementation should live in a small Python execution service rather than
restoring the retired 0.6 direct-tool runtime, job system, diagnostic surface,
or graph adapters. It must not monkey-patch `unreal` functions or install
process-global signal handlers as execution wrappers.

## Dry Run, Transactions, and Side Effects

Full Python cannot provide Loomle's mutation dry-run contract. Source text
cannot be reliably reduced to parse, resolve, validate, and plan phases before
application. Runtime reflection and ordinary Python control flow can choose
operations dynamically.

Examples of effects that an Editor transaction cannot generally reverse
include:

- saving or deleting packages and files;
- changing config, console variables, subsystem, or process-global state;
- spawning processes or performing network requests;
- invoking UE APIs that do not call `Modify`;
- starting asynchronous work that outlives the tool call.

`FScopedTransaction` would therefore provide misleading partial protection.
The initial implementation does not wrap arbitrary code in a Loomle-owned
transaction and does not report `valid`, `planned`, `applied`, `diff`,
revisions, or rollback state.

Even wrapping the call in `FScopedTransaction` and calling `Cancel()` after an
exception would not constitute rollback: cancellation removes the transaction
record but does not generally restore objects that were already changed.

Before this tool becomes current rather than planned, Loomle's mutation policy
must document one narrow raw-execution exception:

- every structured mutation interface still follows the shared dry-run
  contract;
- raw language execution is separately named and permanently marked
  destructive;
- it exposes no dry-run-shaped fields and makes no mutation-planning claim;
- a capability promoted from Python into a structured interface loses the
  exception and must implement the normal dry-run path.

Undo availability is determined entirely by the UE APIs the script chooses.
The result must never imply that the script is undoable.

## Timeout, Cancellation, and Replay

There is no safe generic way to interrupt Python while it may be executing UE
code on the Game Thread. Python `signal.alarm` is unavailable on Windows and
cannot preempt arbitrary native UE work safely. Injecting an exception,
terminating a thread, or releasing the interpreter can corrupt Editor state.
An infinite loop or blocking call can therefore freeze the Editor indefinitely
and may require terminating the Editor process, with loss of unsaved work.

Consequently:

- `python_execute` has no caller-controlled timeout;
- the public tool service does not pass MCP AbortSignal cancellation through
  after dispatch, matching the no-abandonment rule used for mutation;
- the private transport may still send its ordinary cancellation notification
  after a response timeout, but admitted Python does not honor it as a safe
  stop request;
- disconnect or transport timeout after Game Thread admission has an uncertain
  outcome;
- once socket dispatch has been attempted, a Client-side connection failure is
  handled conservatively as an uncertain outcome because a partial write may
  have reached the Bridge and the Client cannot prove whether Game Thread
  admission occurred;
- a timed-out script may still be running and may still produce side effects;
- the Client never retries or replays the request automatically;
- a retry requires the agent to inspect current UE and external state first.

The Bridge's own admission-timeout path remains safe: when it successfully
cancels the admission object before `started`, the queued Game Thread task does
not execute. The existing long execution budget remains a transport/liveness
budget, not a Python termination guarantee. A timeout or post-dispatch transport
failure on this route must be presented as non-retryable and include
`outcomeUnknown=true` and `stateMayHaveChanged=true`; it must say that execution
may still be in progress and must not invite a blind retry.

The current generic Client timeout is marked retryable. The Python route must
override that presentation: once the `python.execute` request frame has been
offered to the socket, timeout or connection loss is non-retryable until the
agent has separately inspected current state. Route-specific Bridge error data
and the Client formatter must preserve the two uncertain-outcome fields instead
of reducing them to the generic retryable timeout. This is an uncertain-outcome
rule, not a claim that another request can never be issued.

## Errors

The initial stable error codes are:

| Code | Meaning | Retry |
| --- | --- | --- |
| `tool.invalid_arguments` | missing, empty, oversized, or unknown input | fix request |
| `runtime.python_unavailable` | the module or target build has no usable Python support | no |
| `runtime.python_initializing` | Python enablement was requested but is not ready | after readiness changes |
| `runtime.python_source_staging_failed` | source staging failed before execution | after filesystem repair |
| `runtime.python_unavailable_during_play` | PIE or another play session is active | after stopping play |
| `runtime.python_execution_failed` | Python or UE exception after execution started | inspect traceback and state |
| `runtime.editor_shutting_down` | the Editor is draining | after restart |
| `runtime.request_timeout` | admitted execution exceeded the transport budget | inspect state; never blind retry |

Project selection, offline, multiple-Editor, startup, admission, and protocol
errors retain their existing codes.

An execution failure is not evidence that no mutation happened. Code can
change state and then throw. Its failure payload uses
`stateMayHaveChanged=true` and includes this warning next to the native
traceback. Pre-execution errors use `stateMayHaveChanged=false`.

## Demand Feedback

The Bridge emits one compact structured UE log entry containing:

- `reason`;
- mode;
- success or error code;
- duration and truncation facts.

It must not log the source code or restore a separate diagnostic store.
`reason` must also avoid credentials or private content because normal UE logs
can persist under `Saved/Logs`.

Fallback usage does not silently expand SAL. Maintainers periodically group
reasons into capability classes. A repeated, stable workflow should produce a
source-grounded interface design and dedicated tests; only then should agents
stop using Python for that workflow.

## Implementation Scope

The first implementation changes:

- Client public tool definitions, routing, validation, formatting, and tests;
- Bridge RPC capability and dispatch;
- a focused Python execution service;
- plugin and module dependency declarations;
- generated private protocol version sources;
- the diagnostic catalog entries for the new `runtime.python_*` codes;
- current Client, interface-guide, lifecycle, dry-run-policy, coverage, and
  release-test documentation;
- packaged end-to-end expectations from seven to eight public tools.

It does not restore:

- the old public `execute` name;
- the 0.6 direct-tool runtime or compatibility aliases;
- Python-driven graph adapters;
- background jobs or idempotency keys;
- arbitrary script-path execution;
- a diagnostic-tail tool;
- a fake dry-run or best-effort rollback mode.

## Verification

### Fast Client tests

Tests must prove:

- exactly eight public tools are listed and `python_execute` has destructive,
  non-read-only, non-idempotent annotations;
- `reason`, `mode`, code size, unknown fields, and empty inputs are validated;
- the bound project and healthy runtime are resolved before dispatch;
- `python.execute` is the exact private tool name and source code is forwarded
  only to that selected runtime;
- runtime errors preserve Python result, log, truncation, and uncertain-outcome
  detail;
- a failure before socket dispatch begins remains a pre-execution transport
  failure, while timeout or connection loss after a write attempt is reported
  with both uncertain-outcome fields;
- an admitted request is not canceled or automatically replayed.

### UE Automation

Native tests must cover:

- `rpc.capabilities` advertises `python.execute`;
- `exec` can import `unreal`, emits ordered logs, and cleans its temporary file;
- `eval` returns the native representation of a simple expression;
- syntax and runtime errors preserve tracebacks and prior log entries;
- native log levels map to stable public values;
- unavailable and failed initialization paths are actionable;
- startup readiness does not busy-wait or initialize Python inside the tool
  call;
- PIE, shutdown, invalid input, staging failure, and size limits fail before
  execution;
- a staging path containing spaces is passed to UE as a quoted normalized
  absolute path;
- temporary files are removed after both success and ordinary failure;
- a script that moves, deletes, or rewrites its own staging file does not make
  cleanup reporting crash or misstate the original path's existence;
- cleanup failure preserves the execution outcome and reports the retained
  source path;
- output truncation is deterministic and never changes success into failure;
- a successful script that calls `unreal.log_error` remains successful;
- no result claims dry run, transaction, rollback, or idempotency;
- failure after an intentional transient UE mutation is reported as an
  uncertain partial outcome rather than a rollback;
- an admitted delayed script can outlive caller abandonment without being
  replayed;
- asynchronous work started by a script is not misreported as completed work.

Tests must not alter a user project. UE Automation uses transient objects or a
copied fixture and performs explicit cleanup.

Ordinary automated suites must not execute a true infinite loop. Any destructive
watchdog exercise of that failure mode must run in a disposable one-off Editor
process whose forced termination and fixture cleanup are owned by the test
harness.

### Packaged end-to-end

The exact release archive must:

1. list all eight public tools;
2. report a ready bound Editor;
3. execute one non-mutating `exec` script that imports `unreal` and logs a
   deterministic value;
4. execute one simple `eval`;
5. surface one controlled Python exception with its traceback;
6. prove the Editor remains responsive afterward;
7. leave no Loomle temporary Python source after normal completion.

Release audit must also prove the Python plugin dependency is present in the
descriptor and the packaged Bridge still contains only the single
`LoomleBridge` module.

## Acceptance

The design is implemented only when:

- the public and private names, schemas, annotations, and errors match this
  document;
- full `import unreal` execution works against the bound UE 5.7 Editor;
- commands request UE unattended behavior without claiming that arbitrary
  Python or UE APIs cannot display or wait for UI;
- code is never advertised as sandboxed, transactional, dry-runnable, or
  safely cancellable;
- timeout and failure diagnostics preserve uncertain-outcome semantics;
- current documentation clearly prefers structured Loomle interfaces and
  identifies this as a capability fallback;
- the root fast gate, complete UE Automation category, and packaged
  end-to-end gate pass on every supported platform.
