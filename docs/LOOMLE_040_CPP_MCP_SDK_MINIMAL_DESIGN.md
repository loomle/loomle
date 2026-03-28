# LOOMLE 0.4.0 Minimal C++ MCP SDK Design

## Summary

`LOOMLE 0.4.0` should introduce a small self-owned C++ MCP server layer inside
`LoomleBridge`.

This layer exists to solve one concrete problem:

- LOOMLE needs a standard MCP server at the Unreal authority boundary
- the current ecosystem does not provide a sufficiently mature official C++ MCP
  SDK for Unreal-hosted use
- relying on a Python-hosted MCP server as the long-term primary runtime path
  adds avoidable lifecycle and transport complexity

The intended direction is therefore:

- `loomle mcp` remains a standard MCP client implemented with the Rust SDK
- `LoomleBridge` becomes a standard MCP server implemented through a minimal
  LOOMLE-owned C++ MCP runtime layer
- existing Unreal authority-side tool execution remains in `LoomleBridge`
- the current custom RPC layer becomes a migration/compatibility layer rather
  than the long-term primary runtime protocol

This document defines the minimum scope of that C++ MCP runtime layer.

## Why A Minimal C++ MCP Layer Is Needed

The current runtime path is still centered on:

- local pipe/socket transport
- a custom `rpc.health` / `rpc.capabilities` / `rpc.invoke` protocol
- Rust MCP server adaptation on top of that RPC layer

That model was effective as an intermediate bridge.

It becomes weaker for `0.4.0` because the product direction now requires:

- `loomle mcp` as the primary agent-facing runtime contract
- strict MCP compatibility at the runtime boundary
- Unreal-side runtime authority
- a session model that does not depend on a custom RPC protocol being the
  primary contract forever

If LOOMLE keeps the current RPC layer as the main runtime contract, then:

- `loomle mcp` is not truly talking to an MCP-native Unreal authority
- tool schema and lifecycle remain filtered through a compatibility layer
- future agent behavior depends on LOOMLE-specific protocol glue rather than
  MCP-native expectations

The alternative of making Unreal-hosted Python the long-term primary MCP server
was considered, but it introduces new complexity around:

- server lifecycle in the embedded Python environment
- pipe/socket ownership
- Unreal editor shutdown and stale endpoint cleanup
- keeping Python as the runtime authority shell instead of Unreal/plugin code

The minimal C++ MCP SDK direction keeps the runtime authority where it already
belongs:

- inside `LoomleBridge`

## Product Goal

The goal is not to create a general public C++ MCP framework.

The goal is to create the smallest standards-correct MCP runtime layer needed
for:

- Unreal-hosted LOOMLE runtime sessions
- current tool families
- current `jobs` and `profiling` behavior
- clean future migration away from the custom RPC main path

This is a product infrastructure layer, not a standalone ecosystem project.

## Design Principles

### 1. Implement Only The MCP Surface LOOMLE Actually Needs

The first version should implement only the parts of MCP required by the
current LOOMLE runtime.

### 2. Keep Unreal Authority In `LoomleBridge`

The new layer should not replace current Unreal-side tool execution logic.

It should sit above that logic as a standards-correct MCP shell.

### 3. Preserve Existing Tool Execution Investment

Current authority-side execution in `LoomleBridge` already includes:

- tool routing
- game-thread dispatch
- `jobs`
- `profiling`
- `graph.*`
- `execute`

The new C++ MCP layer should adapt this execution surface rather than rewrite
it immediately.

### 4. Separate Transport From Protocol

The project-scoped pipe/socket transport can remain in the first `0.4.0`
phase.

What changes is the protocol carried over that transport:

- from custom RPC
- to standard MCP

### 5. Keep The New Layer Replaceable

The C++ MCP layer should be small enough that future migration is still
possible if a strong official/community C++ MCP SDK emerges.

## What The Minimal C++ MCP Layer Must Do

The first version must support only the MCP features LOOMLE currently depends
on.

### Required Message Model

Support:

- request
- response
- notification
- id
- method
- params
- result
- error

This is the JSON-RPC base LOOMLE needs in order to host MCP sessions.

### Required Lifecycle

Support:

- `initialize`
- `notifications/initialized`

Initialization must return:

- `protocolVersion`
- `capabilities`
- `serverInfo`

### Required Capabilities

The first version should expose:

- `tools`

That is the only capability required for the current runtime surface.

### Required Tool Methods

Support:

- `tools/list`
- `tools/call`

This is sufficient for the current LOOMLE runtime product contract.

### Required Session Model

The server must support a long-lived connection/session.

It should not behave like the current one-request custom RPC pattern.

## Explicitly Out Of Scope For The First Version

The minimal C++ MCP layer should not implement these in its first release:

- resources
- prompts
- completions
- sampling
- roots
- streamable HTTP
- cancellation
- progress notifications
- server-side pagination
- tool list change notifications
- authentication
- multi-client orchestration beyond what is required for the local transport

If any of these are needed later, they should be added intentionally rather
than included preemptively.

## Proposed Internal Modules

The first version should be divided into a very small set of internal modules.

### 1. `McpMessages`

Owns:

- parsing MCP/JSON-RPC request envelopes
- building responses
- building notifications
- encoding errors

### 2. `McpServerSession`

Owns:

- per-connection lifecycle
- initialize state
- initialized state
- method dispatch for MCP methods

### 3. `McpToolRegistry`

Owns:

- tool descriptors
- lookup by tool name
- tool metadata used by `tools/list`

### 4. `McpTransport`

Owns:

- abstract send/receive interface for MCP messages

This should be an internal interface, not a large transport framework.

### 5. `McpPipeTransport`

Owns:

- project-scoped unix socket / named pipe transport binding
- adapting the existing local transport model to session-oriented MCP traffic

### 6. `LoomleMcpBridge`

Owns:

- connecting `tools/call` to Unreal authority-side execution
- mapping current `LoomleBridge` tool dispatch into MCP tool handling

## Relationship To Existing `LoomleBridge` Runtime Execution

The minimal C++ MCP layer should not initially replace current tool execution.

Instead, it should call into a formalized Unreal authority execution surface.

Directionally, that surface should be evolved from the current internal
dispatcher that already exists today:

- request routing
- game-thread dispatch
- tool execution
- job runtime

The desired relationship is:

```text
loomle mcp (Rust MCP client)
  -> project-scoped local transport
  -> LoomleBridge minimal C++ MCP server
  -> Unreal authority-side tool execution
```

This replaces the current long-term assumption that the primary runtime path is:

```text
loomle mcp
  -> Rust MCP server
  -> custom RPC
  -> LoomleBridge
```

## Required Tool Integration Model

The first version should make `tools/call` route into the existing Unreal
authority execution model rather than reimplement every tool.

That means:

- `context`
- `jobs`
- `profiling`
- `execute`
- `graph.*`
- editor tools
- diagnostics tools

should continue to be executed by current `LoomleBridge` authority logic.

The new layer is responsible for:

- standard MCP lifecycle
- standard `tools/list`
- standard `tools/call`
- standards-correct error/result envelopes

It is not initially responsible for redesigning tool internals.

## Tool Registry Rules

The tool registry in the C++ MCP layer should be a direct standards-facing
source of truth for:

- tool name
- description
- input schema

This registry may initially reuse or derive from the existing LOOMLE tool
descriptor source, but the runtime-facing `tools/list` response should come
from the MCP layer itself rather than from the legacy RPC capabilities call.

## Error Model

The minimal C++ MCP layer should:

- preserve MCP error semantics at the protocol level
- preserve existing LOOMLE tool-domain errors in structured tool results

This means:

- invalid MCP method usage should become MCP protocol errors
- tool execution failures should remain tool-level structured failures

The layer should not collapse tool-domain failures into generic protocol
errors.

## Transport Direction

The first `0.4.0` transport direction should remain:

- Unix: project-scoped socket
- Windows: project-scoped named pipe

This document does not require changing transport medium.

It requires changing what protocol runs over that transport:

- from custom LOOMLE RPC
- to standard MCP

## Relationship To `loomle mcp`

`loomle mcp` should continue to be implemented with the Rust MCP client SDK.

It should behave as a normal MCP client:

- connect
- initialize
- send `notifications/initialized`
- `tools/list`
- `tools/call`

It should not retain LOOMLE-specific compatibility assumptions once the C++ MCP
server path becomes primary.

## Relationship To The Existing Custom RPC Layer

The custom RPC layer should be treated as a migration/compatibility layer.

It should not remain the long-term primary runtime contract for `0.4.0`.

Migration direction:

1. introduce the C++ MCP server layer
2. keep custom RPC available while compatibility is needed
3. migrate the primary `loomle mcp` runtime path to the C++ MCP server
4. demote the custom RPC path to repair/compatibility/debug use only

## Why This Is Better Than A Python-Hosted Primary Server

This direction is preferable because:

- Unreal authority already lives in `LoomleBridge`
- current execution infrastructure already lives in `LoomleBridge`
- transport lifecycle already lives in plugin/runtime code
- `jobs` and `profiling` already fit naturally on the Unreal authority side
- agent-facing protocol becomes standard MCP without requiring Python to own
  the runtime boundary

The Python environment can still remain important for:

- `execute`
- specific runtime helpers
- future scripting surfaces

But it should not be required to own the primary runtime server boundary if
that can be kept cleaner in C++.

## Rollout Phases

### Phase 1: Minimal MCP Server Runtime

Implement:

- initialize
- initialized
- tools/list
- tools/call
- local transport session handling

And route `tools/call` into the current Unreal authority execution surface.

### Phase 2: Make It The Primary Runtime Path

Update `loomle mcp` to connect to the new MCP-native project endpoint as the
primary path.

Keep the old RPC path as compatibility fallback only if required.

### Phase 3: Collapse Redundant RPC Translation

As MCP-native runtime handling stabilizes:

- reduce or remove the legacy RPC protocol from the normal runtime flow
- preserve only what is still necessary for migration/repair

## Acceptance Criteria

The first minimal C++ MCP layer is complete when:

1. `loomle mcp` can establish a standard MCP session directly with Unreal-side
   runtime authority.
2. `initialize`, `notifications/initialized`, `tools/list`, and `tools/call`
   work without a Rust-side MCP-to-custom-RPC adaptation layer in the primary
   path.
3. Current LOOMLE tools remain available through the MCP-native path.
4. `jobs` and `profiling` still behave correctly over the MCP-native path.
5. The local transport remains project-scoped and deterministic.
6. The design stays narrow enough that it can later be replaced or generalized
   if a stronger C++ MCP SDK becomes viable.

## First-Phase Implementation Cut

The first implementation phase should be intentionally narrow.

Its goal is not to finish the whole `0.4.0` runtime migration.

Its goal is to prove one complete MCP-native path inside `LoomleBridge`:

- local transport
- MCP initialize lifecycle
- `tools/list`
- `tools/call`
- authority-side execution bridge

### Phase 1 Scope

Phase 1 should deliver:

1. `mcp core` message/runtime types
2. per-connection MCP session handling
3. runtime-backed tool registry
4. MCP-native `initialize`
5. MCP-native `tools/list`
6. MCP-native `tools/call`
7. bridge from `tools/call` to current `DispatchTool`
8. transport host integration over existing local pipe/socket transport

Phase 1 should not deliver:

- new tool business logic
- resources/prompts/completions
- progress/cancel
- HTTP transport
- multi-project routing
- install/doctor redesign

## Recommended Engineering Task Breakdown

### Task 1: Add `mcp core` module skeleton

Create the internal module layout for:

- messages
- server_info
- tool_registry
- session
- tool_bridge
- transport_host

The first task is structural only:

- module boundaries
- types
- no full runtime switch yet

### Task 2: Add minimal MCP message and session types

Implement:

- request parsing
- response building
- notification building
- initialize state tracking
- per-connection session state

This task should make it possible to host:

- `initialize`
- `notifications/initialized`

without yet wiring every tool path.

### Task 3: Introduce `McpCoreToolRegistry`

Add the runtime registry type and load the active LOOMLE tool descriptors into
it.

This task should make:

- `tools/list`

possible from `mcp core`.

The first version may reuse existing descriptor definitions as migration input,
but the session runtime should read descriptors from the registry, not from the
old RPC capabilities path.

### Task 4: Add `McpCoreToolBridge`

Introduce the narrow bridge that accepts:

- tool name
- MCP arguments object

and routes execution into the existing Unreal authority execution primitive.

The first version should reuse:

- `DispatchTool`

and should not rewrite the existing `Build*ToolResult` functions.

### Task 5: Add `McpCoreTransportHost`

Introduce connection-aware host logic above the current local transport
substrate.

This task should:

- create one session per connection
- route incoming JSON messages into that session
- write outgoing messages back to the correct connection

The target is to replace the old:

- request callback in
- one response out

with:

- connection opened
- message received
- message emitted
- connection closed

### Task 6: Teach `FLoomlePipeServer` to host connection-aware MCP traffic

Refactor the integration point currently used by:

- `HandleRequest`

so the transport no longer assumes the old RPC pattern.

This task should preserve:

- project-scoped endpoint ownership
- socket/pipe lifecycle
- per-connection write support

It should not preserve:

- the old RPC request callback model as the primary path

### Task 7: Prove a complete MCP-native loop

The first end-to-end proof should be:

1. connect over the existing local project-scoped transport
2. send `initialize`
3. send `notifications/initialized`
4. send `tools/list`
5. send `tools/call` for a simple runtime-backed tool

The initial proof tool should be one that is low-risk and already well
behaved.

Recommended first proof candidates:

- `context`
- `diag.tail`
- `loomle`

Then expand to:

- `execute`
- `jobs`
- `profiling`
- `graph.*`

## Suggested File/Module Landing

The first version should keep `mcp core` close to `LoomleBridge` runtime code.

Directionally, a layout like this is appropriate:

```text
engine/LoomleBridge/Source/LoomleBridge/Private/mcp_core/
  LoomleMcpCoreMessages.*
  LoomleMcpCoreServerInfo.*
  LoomleMcpCoreToolRegistry.*
  LoomleMcpCoreSession.*
  LoomleMcpCoreToolBridge.*
  LoomleMcpCoreTransportHost.*
```

The exact file split can be adjusted, but the important part is:

- `mcp core` remains clearly separate from graph/runtime business logic
- it is not hidden inside one large replacement for the old RPC file

## Phase 1 Success Criteria

Phase 1 should be considered successful when all of the following are true:

1. a client can speak standard MCP over the existing local project-scoped
   transport
2. `initialize` and `notifications/initialized` are handled by `mcp core`
3. `tools/list` is served from `McpCoreToolRegistry`
4. `tools/call` is bridged into current authority-side tool execution
5. at least one nontrivial existing runtime tool works end to end through the
   MCP-native path
6. the old custom RPC path is no longer required to prove the primary runtime
   contract

## Decision

`LOOMLE 0.4.0` should adopt a self-owned minimal C++ MCP server layer inside
`LoomleBridge` rather than treating the current custom RPC layer or a
Python-hosted MCP server as the long-term primary runtime architecture.

This should be treated as:

- a small standards-correct runtime boundary
- an Unreal-authority-native server surface
- a deliberately narrow product infrastructure layer

not as a general-purpose public C++ MCP framework.

## Naming

This layer should be referred to consistently as:

- `mcp core`

This name is intentionally narrow.

It emphasizes that the layer is:

- the minimum standards-correct MCP runtime core inside `LoomleBridge`
- not a general-purpose public SDK
- not a broad transport framework
- not a second product runtime

## First-Version `mcp core` Shape

The first version of `mcp core` should be defined around five pieces:

1. `McpCoreMessage`
2. `McpCoreServerInfo`
3. `McpCoreToolRegistry`
4. `McpCoreSession`
5. `McpCoreToolBridge`

This is enough to replace the current custom RPC runtime boundary with a
standards-correct MCP-native boundary.

## 1. `McpCoreMessage`

`McpCoreMessage` owns the MCP/JSON-RPC wire envelope.

The first version only needs to support:

- request messages
- response messages
- notification messages
- typed errors

It should not attempt to model every future MCP feature up front.

The first version should provide:

- parse request from JSON
- build success response
- build protocol error response
- build notification message
- serialize message to JSON

## 2. `McpCoreServerInfo`

`McpCoreServerInfo` owns the data returned during `initialize`.

It replaces the old role previously played by:

- `rpc.health`
- `rpc.capabilities`

at the protocol-entry boundary.

The first version should expose:

- `protocolVersion`
- `capabilities`
- `serverInfo`
- optional `instructions`

Directionally, the initialize result should look like:

```json
{
  "protocolVersion": "2025-06-18",
  "capabilities": {
    "tools": {}
  },
  "serverInfo": {
    "name": "loomle-unreal-runtime",
    "version": "0.4.0"
  },
  "instructions": "LOOMLE Unreal runtime server."
}
```

The exact protocol version should match the version LOOMLE chooses to support
at implementation time. The key design rule is:

- it must be standard MCP initialize output
- it must not be a LOOMLE-specific substitute for initialize

## 3. `McpCoreToolRegistry`

`McpCoreToolRegistry` owns the standards-facing tool descriptor surface.

The first version should store, for each tool:

- tool name
- description
- input schema JSON

Optional future fields such as output schema can be added later if needed.

The first version should expose operations like:

- register tool descriptor
- find tool by name
- list all tools

This registry should become the source of truth for:

- `tools/list`
- name validation during `tools/call`

It should not depend on the old custom RPC capabilities response.

## Tool Schema Source Of Truth

`LOOMLE 0.4.0` should avoid ending up with long-term duplicated schema
authority across:

- legacy Rust transitional schema
- C++ MCP runtime schema
- documentation-only schema fragments

Before the native `mcp core` cutover, the practical tool descriptor source
lived in the Rust transitional runtime. After cutover, that source now lives in:

- [McpCoreTools.cpp](/Users/xartest/dev/loomle/engine/LoomleBridge/Source/LoomleBridge/Private/mcp_core/McpCoreTools.cpp)

And current documentation already treats:

- `tools/list`

as the runtime source of truth for accepted tool schemas.

That direction should be preserved in `0.4.0`.

### First-Phase Rule

For the first `mcp core` implementation phase:

- `McpCoreToolRegistry` should become the runtime source of truth for the
  Unreal-hosted MCP server path
- `tools/list` should be generated directly from that registry
- name validation during `tools/call` should use that same registry

### Migration Rule

During migration from the current Rust-MCP-server-centered layout:

- the existing Rust schema source may remain the authoring reference briefly
- but the target runtime source of truth must move into `mcp core`
- long term, the runtime should not require a Rust-only schema authority in
  order to describe tools

### Practical Direction

The intended long-term direction is:

- one canonical tool descriptor definition source
- one runtime registry derived from that source
- `tools/list` emitted directly from the runtime registry

This can be achieved in either of two acceptable ways:

1. move the canonical descriptor source into a shared format consumed by both
   Rust and C++
2. move the canonical runtime descriptor source into C++ and treat the old Rust
   schema as transitional

For `0.4.0`, option 2 is the simpler and more aligned direction because the
runtime authority itself is moving into `LoomleBridge`.

### Immediate Design Rule

The first `mcp core` design should assume:

- `McpCoreToolRegistry` is the authoritative runtime descriptor store
- Rust `loomle mcp` should discover schemas through standard `tools/list`
- documentation should describe `tools/list` as the runtime contract
- no new long-term design should depend on Rust-side hardcoded tool schemas

## Tool Registry Relationship To Current Runtime Tools

The first `mcp core` registry should cover the currently active runtime tool
surface, including:

- `loomle`
- `context`
- `execute`
- `jobs`
- `profiling`
- `editor.open`
- `editor.focus`
- `editor.screenshot`
- `graph`
- `graph.list`
- `graph.resolve`
- `graph.query`
- `graph.mutate`
- `graph.verify`
- `diag.tail`

This keeps the registry aligned with the current active product tool surface
rather than with removed historical tools.

## Tool Registry Design Rules

The first version should follow four rules:

1. tool descriptor metadata must be runtime-owned, not documentation-owned
2. `tools/list` must be emitted directly from `McpCoreToolRegistry`
3. `tools/call` must validate requested tool names against the same registry
4. the registry must not depend on the old RPC capabilities model

These rules prevent `mcp core` from inheriting a split-brain schema model.

## 4. `McpCoreSession`

`McpCoreSession` owns per-connection MCP lifecycle.

The first version should support:

- `initialize`
- `notifications/initialized`
- `tools/list`
- `tools/call`

The session should track at least:

- whether initialization has completed
- whether `notifications/initialized` has been observed
- whether the transport is still open

It should reject or error on invalid ordering where required by MCP.

### Session Method Routing

The first version should route methods like this:

- `initialize` -> `McpCoreServerInfo`
- `notifications/initialized` -> mark session ready
- `tools/list` -> `McpCoreToolRegistry`
- `tools/call` -> `McpCoreToolBridge`

No LOOMLE-specific substitute methods should exist in the primary path.

## 5. `McpCoreToolBridge`

`McpCoreToolBridge` is the layer that connects MCP `tools/call` to current
Unreal authority-side tool execution.

This is the most important bridge in the first version.

It should:

- accept MCP `tools/call` input
- validate tool name against `McpCoreToolRegistry`
- hand tool execution to the existing authority-side dispatcher
- translate the existing structured tool payload into MCP result form

This means the first version should not reimplement all tools.

It should reuse the current authority-side execution surface.

## First-Version Wire Contract

The first version should fully support the following MCP lifecycle:

1. client sends `initialize`
2. server responds with standard initialize result
3. client sends `notifications/initialized`
4. client calls `tools/list`
5. client calls `tools/call`

Nothing else is required for first delivery.

## `tools/list` Shape

`tools/list` should return standard MCP tool descriptors derived from
`McpCoreToolRegistry`.

Directionally:

```json
{
  "tools": [
    {
      "name": "execute",
      "description": "Run Unreal Python code.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "code": { "type": "string" }
        },
        "required": ["code"]
      }
    }
  ]
}
```

The exact schema contents can continue to derive from LOOMLE's existing tool
descriptor source.

## `tools/call` Shape

The first version should accept standard MCP `tools/call` requests:

```json
{
  "name": "execute",
  "arguments": {
    "code": "print('hello')"
  }
}
```

The bridge should route this into current Unreal authority execution and return
either:

- structured success content
- structured tool error content

Directionally:

```json
{
  "structuredContent": {
    "ok": true,
    "result": "hello"
  },
  "isError": false
}
```

Or:

```json
{
  "structuredContent": {
    "code": "INVALID_ARGUMENT",
    "message": "arguments.code is required."
  },
  "isError": true
}
```

The key rule is:

- MCP protocol errors remain protocol errors
- tool-domain failures remain structured tool results

## Relationship To Existing `DispatchTool`

The current `DispatchTool` path in `LoomleBridge` should be reinterpreted as
the first authority-side execution primitive behind `mcp core`.

This means:

- it should no longer be thought of as a child of the long-term RPC protocol
- it should become the execution bridge used by `McpCoreToolBridge`

Directionally, the first implementation can be:

```text
McpCoreSession
  -> McpCoreToolBridge
  -> DispatchTool
  -> existing Build*ToolResult handlers
```

This keeps the first version small and low-risk.

## Expected Refactor Of Current Runtime Layers

Once `mcp core` becomes primary, the old runtime layers should be reinterpreted
like this:

- old custom RPC wire layer -> removed from the primary path
- `DispatchTool` -> retained as authority-side execution bridge
- `Build*ToolResult` -> retained as tool implementation layer
- pipe/socket host -> retained as local transport substrate

This is a narrow refactor, not a runtime rewrite.

## Minimal Module Boundaries

The first version should keep module boundaries explicit.

Recommended logical split:

### `mcp_core/messages`

- JSON parsing and serialization
- request/response/notification envelopes

### `mcp_core/server_info`

- initialize result
- protocol version
- server name/version
- capabilities

### `mcp_core/tool_registry`

- tool metadata source
- `tools/list` backing store

### `mcp_core/session`

- lifecycle state
- method routing
- call entrypoints

### `mcp_core/tool_bridge`

- call into authority-side runtime execution
- map LOOMLE payloads to MCP tool results

### `mcp_core/transport`

- transport adapter interface
- local pipe/socket integration

## What `mcp core` Should Not Absorb

To keep the layer healthy, `mcp core` should not absorb:

- graph logic
- jobs runtime logic
- profiling logic
- execute implementation logic
- editor/runtime business logic
- install/project-discovery logic

Those belong elsewhere.

`mcp core` should remain:

- protocol core
- session core
- tool registry core
- execution bridge

## First Implementation Cut

The first implementation cut should be as small as possible:

1. add `mcp core` message/session/types
2. add `initialize`
3. add `tools/list`
4. add `tools/call`
5. bridge `tools/call` into current `DispatchTool`
6. keep local pipe/socket transport
7. make `loomle mcp` connect through standard MCP directly to this server

This first cut is enough to prove the new runtime boundary.

## Transport Host Direction

The first `mcp core` version should preserve the current local transport
medium:

- Unix: project-scoped socket
- Windows: project-scoped named pipe

But it should stop using the old request/response RPC hosting shape.

The transport host direction for `0.4.0` should be:

- keep `FLoomlePipeServer` as the local transport substrate
- remove the assumption that transport input is `HandleRequest(line) -> line`
- introduce a connection-aware MCP transport host above the pipe/socket layer

## Why The Current Request Callback Shape Must Change

The current custom RPC transport shape assumes:

- one incoming request string
- one synchronous response string

That fits the old `rpc.health` / `rpc.invoke` model.

It is not the correct long-term host shape for MCP because MCP is
session-oriented and must support:

- multiple messages on one connection
- lifecycle notifications
- multiple tool calls on one connection
- future server-originated notifications

Therefore the local transport host should no longer be modeled as:

- request callback in
- one response string out

## Proposed Transport Host Split

The first `mcp core` transport integration should use three pieces:

### 1. `FLoomlePipeServer`

Continues to own:

- project-scoped socket / named pipe creation
- accept loop
- connection lifecycle
- raw message send/receive
- shutdown
- stale endpoint cleanup

It should not own:

- MCP lifecycle semantics
- tool routing
- tool execution

### 2. `McpCoreTransportHost`

New thin bridge layer that owns:

- per-connection session mapping
- incoming message handoff to the correct `McpCoreSession`
- outgoing message writeback to the correct connection

This layer is intentionally narrow.

It exists only to bind:

- transport connections
- `McpCoreSession`

### 3. `McpCoreSession`

Owns:

- MCP lifecycle state
- MCP method handling
- `tools/list`
- `tools/call`

It should not know platform transport details.

## Connection Model

The first `mcp core` transport host should assume:

- one transport connection
- one `McpCoreSession`

This means session state is per connection, not global.

Each connection should track at least:

- connection id
- session object
- whether initialization completed
- whether the connection is still open

## Recommended Transport Host Interface

The old conceptual interface:

- `HandleRequest(line) -> line`

should be replaced by a connection-aware host model.

Directionally, the host should look more like:

- `HandleConnectionOpened(connectionId)`
- `HandleConnectionMessage(connectionId, rawJsonMessage)`
- `HandleConnectionClosed(connectionId)`

And the transport should expose a write primitive like:

- `SendMessageForConnection(connectionId, rawJsonMessage)`

This gives `mcp core` the correct shape for long-lived sessions.

## Outgoing Message Model

`McpCoreSession` should not assume that every inbound message produces exactly
one outbound message.

Instead, the session should be able to emit:

- zero outgoing messages
- one outgoing message
- multiple outgoing messages

This is required because MCP must support:

- notifications
- lifecycle sequencing
- future server-originated messages

Even if the first implementation mostly emits one response per request, the
host shape should not lock LOOMLE into the old RPC pattern.

## Relationship To Existing Pipe Server Code

The current `FLoomlePipeServer` already contains useful local-transport pieces,
including:

- connection tracking
- per-connection write support
- project-scoped endpoint ownership

Those parts should be retained.

What should change is the protocol integration point above them.

Directionally, the current startup path:

```text
FLoomlePipeServer
  -> request callback
  -> HandleRequest
  -> custom RPC
```

should become:

```text
FLoomlePipeServer
  -> McpCoreTransportHost
  -> McpCoreSession
  -> McpCoreToolBridge
  -> authority-side execution
```

## Transport Host Design Rules

The first `mcp core` transport host should follow three hard rules:

1. transport host does not understand tool semantics
2. `mcp core` session does not understand platform socket/pipe details
3. session state is per connection, not process-global

These rules keep the runtime boundary clean and prevent the transport layer
from becoming a second business protocol layer.

## First Implementation Cut For Transport Host

The smallest transport-host implementation should:

1. keep the current project-scoped local socket/pipe
2. add connection-aware callbacks above `FLoomlePipeServer`
3. create one `McpCoreSession` per connection
4. route raw JSON messages into that session
5. write session-produced outgoing messages back through the existing
   per-connection transport write path

This is enough to support a correct first MCP-native runtime session model.
