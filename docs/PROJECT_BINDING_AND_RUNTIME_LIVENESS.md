# Project Binding And Runtime Liveness

## Intent

One Loomle MCP process is one agent session. The session binds to one Unreal
project, while the Editor process serving that project is a replaceable
runtime. Project selection is Client control-plane state; it is not SAL syntax
and is not repeated on `sal_query`, `sal_patch`, or `editor_context`.

```text
MCP session
  -> stable project binding
      -> current healthy Editor runtime
```

The Client process may remain online while the bound project is offline. Static
`sal_schema` remains available in that state. Every UE-backed call must fail
quickly and specifically instead of waiting for the normal operation timeout or
falling through to another project.

## Public Project Tool

The single public control-plane tool is `project`.

```json
{}
```

With no selector it refreshes discovery and reports the current session
binding plus compact known-project candidates.

```json
{ "projectId": "<stable-project-id>" }
```

```json
{ "projectRoot": "/path/to/project" }
```

Exactly one selector may be supplied. A selector atomically binds or switches
the MCP session. `projectRoot` is the directory containing the `.uproject`;
`projectId` is the stable identity returned by an earlier `project` call.

Binding does not require a running Editor. A valid offline project becomes the
session's bound project and reports `offline`; later UE calls remain directed
to it and become available when a compatible runtime appears. An invalid
selector leaves the previous binding unchanged.

The tool returns concise ordinary MCP text, not SAL Object Text. Each project
entry contains only the information needed to choose and diagnose it:

- `projectId`;
- name;
- `projectRoot`;
- whether it is the session binding;
- `ready`, `offline`, `starting`, `unresponsive`, `incompatible`, or
  `multiple_editors` status.

Runtime ids, endpoints, PIDs, and heartbeat detail are private diagnostics and
are not part of the normal agent workflow.

The `project` tool changes only Client session state. It never edits Unreal
objects, packages, project files, or global Loomle state. Repeating the same
binding is idempotent.

## Session Binding

The session starts unbound and may auto-bind only when one project is
unambiguous. Candidate hints are considered in this order:

1. an explicit `LOOMLE_PROJECT_ROOT` deployment binding;
2. MCP Roots supplied by the host;
3. the Client working directory, only when the host does not support Roots;
4. the sole known project when exactly one candidate remains.

MCP Roots are a connection-level set, not an active-root or per-request signal.
Multiple Roots that all identify the same project may auto-bind it. Roots that
identify different projects, or a monorepo Root containing multiple projects,
must leave the session unbound. Root order, Editor focus, start time, PID, and
last response time are never selection rules.

Once established, a binding is sticky. Roots changing, another project
starting, or the current project becoming unavailable never switches it.
Only another successful `project` call changes the binding. Each in-flight
request keeps the immutable binding snapshot with which it started.

Different MCP processes may bind different projects concurrently. Loomle does
not write a global active/default project file.

If a bound project has more than one live Editor process and no existing ready
runtime affinity, Loomle reports `project.multiple_editors`. A ready affinity
remains stable while other Editors appear. If the affined Editor becomes
starting, stale, unresponsive, or incompatible, Loomle reports that state and
does not jump to another Editor; only disappearance of its unique runtime
releases the affinity. The initial public contract does not expose
Editor-instance selection and never guesses between processes.

## Project And Runtime Identity

`projectId` is stable for one normalized project-root identity. Separators and
trailing delimiters are normalized; case is folded only on Windows, while
case-sensitive platforms preserve it. `runtimeId` is a new random identity for
every Editor process. Every runtime owns a unique record and transport
endpoint:

The Client must reproduce that identity independently of its host test
platform. Windows roots use Win32 absolute-path resolution, forward slashes,
and case folding for identity and comparison. macOS and Linux roots use POSIX
absolute-path resolution and preserve case. A platform argument selects both
the resolver and the comparison rules; it must not select POSIX case rules
while still calling the Windows host resolver.

`projectRoot` remains a usable absolute filesystem path rather than the folded
identity string. Direct offline discovery preserves the platform-native
absolute path spelling for display and file access, while `projectId` is
derived from the UE-compatible normalized identity. Runtime health and
selection compare roots through the normalized identity, so a Windows root
written by UE with forward slashes still matches the equivalent native Client
path. MCP `file:` Roots are converted with the host URL implementation before
the same project matching rules are applied.

```text
~/.loomle/state/runtimes/<runtimeId>.json
```

```json
{
  "schemaVersion": 2,
  "runtimeId": "<per-process-guid>",
  "projectId": "<stable-project-id>",
  "name": "Game",
  "projectRoot": "/path/to/Game",
  "uproject": "/path/to/Game/Game.uproject",
  "endpoint": "<unique-local-endpoint>",
  "pid": 1234,
  "protocolVersion": 2,
  "startedAt": "<utc-time>"
}
```

Records are discovery candidates, not proof of availability. The Client must
validate the live endpoint with `rpc.health`, including exact `runtimeId`,
`projectId`, normalized `projectRoot`, lifecycle state, and protocol version.
Records whose stored project id does not match the canonical or known legacy
identity of their root are ignored. Stale or corrupt records cannot make a
project appear ready or route work across projects.

When the Editor restarts, the binding remains on `projectId`; the Client may
connect to that project's new unique runtime. It must never resolve the binding
to a runtime belonging to another project.

## Bridge Lifecycle

The Bridge lifecycle is:

```text
starting -> ready -> draining -> offline
              \-> failed
```

Creating the pipe-server thread is not readiness. The Bridge publishes its
runtime record only after:

1. the Unix socket has successfully bound and entered `listen`, or the Windows
   named pipe listener has been created successfully;
2. UE Editor initialization is complete; and
3. the Game Thread has demonstrated forward progress.

`ready` means the Editor RPC host can accept work. It does not mean every
asynchronous Asset Registry, shader, DDC, or later mount scan is idle; each
interface waits or reports pending according to the corresponding UE API.

`rpc.health` is handled without Game Thread dispatch and returns the immutable
runtime/project identities, protocol version, lifecycle state, listener state,
and recent Game Thread progress. Only `ready` is invokable.

UE 5.7 lifecycle mapping follows native engine boundaries:

- the module may create its listener during the `PostEngineInit` loading
  phase, but that phase still runs inside `FEngineLoop::Init`;
- `FEditorDelegates::OnEditorInitialized`, after `FEngineLoop::Init`, startup
  map loading, and core editor-service initialization, establishes Loomle's
  publication boundary;
- a lightweight Loomle-owned monotonic heartbeat observes Game Thread progress,
  and every completed admitted request refreshes the same progress evidence;
- `FEditorDelegates::OnShutdownPostPackagesSaved` begins normal draining;
- `FEditorDelegates::OnEditorPreExit` or
  `FCoreDelegates::OnEnginePreExit` provides an idempotent shutdown fallback;
- `OnPreExit` remains a final fallback, not the primary unpublish point.

On Windows, every named-pipe instance uses overlapped I/O. The listener,
connection reader, and serialized response writer each own their operation's
`OVERLAPPED` state. This matches Win32's duplex concurrency model: a pending
read must not serialize a response write behind the next client frame.
Shutdown calls `CancelIoEx` before closing the instance, and connection serials
remain the authority that prevents a completed request from writing to a
replacement connection.

Draining first removes the runtime record and stops admitting new work, then
closes the listener and active transport sessions. Each runtime removes only
its own record and endpoint.

## Invocation Admission

A successful health probe proves that the listener is live, but it cannot
guarantee that the Game Thread will accept a newly queued operation. Every
`rpc.invoke` therefore has two budgets:

1. a short admission budget, approximately two seconds, for the queued Game
   Thread task to atomically enter `started`;
2. the existing long execution budget after admission.

If admission expires, the worker atomically cancels the not-yet-started task
and returns `runtime.editor_unresponsive`. A task that reaches the Game Thread
later must observe cancellation and exit without reading or modifying UE.
This rule is required for Patch safety: Loomle must not report failure and then
apply the edit later.

Once a task has entered `started`, it is not replayed automatically. A lost
Patch response remains an uncertain outcome and follows the existing no-replay
contract.

A long native UE operation may legitimately keep the Game Thread busy beyond
the heartbeat freshness window. Its successful completion refreshes progress
before the response is released, so the next health probe does not mistake
completed work for an unresponsive Editor.

## Client Health And Connections

Before every UE-backed request, the Client resolves only within the bound
project and performs a short connect plus `rpc.health` probe. File existence,
PID liveness, and a named-pipe-shaped string are cheap candidate filters only.

Connection and health discovery use a short bounded timeout. After a healthy
compatible runtime is selected, the Client's normal transport timeout covers
the complete `rpc.invoke`, including admission and execution with delivery
headroom. The Bridge's execution budget starts only after admission. Fatal
transport or identity failures discard that runtime connection. Concurrent
project sessions do not share mutable selection state.

## Errors

Public diagnostics separate project intent from runtime failure:

| Code | Meaning |
| --- | --- |
| `project.selection_required` | no project is bound and candidates are ambiguous |
| `project.not_found` | the requested project identity is unknown or invalid |
| `project.offline` | the bound project has no live runtime |
| `project.multiple_editors` | the bound project has multiple live or unresolved runtimes |
| `runtime.starting` | its runtime exists but is not ready |
| `runtime.editor_unresponsive` | the Game Thread did not admit the request |
| `runtime.editor_shutting_down` | the runtime is draining |
| `runtime.incompatible` | Client and Bridge protocol versions do not match |

Selection, offline, starting, and health failures occur before UE execution and
may be retried after state changes. A timed-out or disconnected request that
was already admitted is never blindly replayed.

## Acceptance

The implementation is complete only when tests prove:

- one session can inspect and bind an online or offline project;
- two sessions can bind different projects independently;
- an offline bound project never falls through to another online project;
- an Editor restart preserves project intent and selects the new runtime;
- stale records fail live identity/health validation;
- two Editors for one project produce an explicit conflict;
- listener startup failure never publishes a ready record;
- a listening runtime remains `starting` until
  `FEditorDelegates::OnEditorInitialized`;
- every completed admitted request refreshes Game Thread progress before its
  response is released;
- shutdown unpublishes before late engine teardown;
- a missed Game Thread admission cannot execute later;
- `sal_schema` remains available without any bound or online project.
