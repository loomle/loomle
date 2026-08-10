# Loomle Client

This directory contains the standalone TypeScript MCP Client for Loomle 0.7.

The workspace package version is intentionally `0.0.0`. The product version
comes only from the repository root and is compiled into the MCP Server through
the generated `src/generated/product-version.ts` module.

It translates compact SAL Text through the shared SAL library and connects to
one running Unreal Editor Bridge over its local JSON-RPC socket.

The Client composes the generic `@loomle/sal` language package with the
UE-specific static catalog from `@loomle/interfaces`. It does not own or
duplicate either contract.

## Public Tools

The Client exposes eight MCP tools:

- `status`: inspect Client identity and update availability plus the bound
  session and Bridge health;
- `project`: inspect project availability and bind this MCP session to one
  project;
- `sal_query`: parse, validate, execute, and format SAL Query Text;
- `sal_patch`: parse, validate, execute, and format SAL Patch Text;
- `sal_schema`: expose the resident SAL guide, then return the active interface
  index or one static interface when called;
- `agent_skill`: list or load resident Loomle workflow Skills compiled from the
  repository-root `skills/` source;
- `editor`: observe the current UE interaction target, or idempotently open,
  focus, and close an exact Blueprint Editor or Blueprint Graph document;
- `python`: run unrestricted Unreal Editor Python as a fallback when no
  structured Loomle interface covers the task, then poll only when a running
  result supplies an execution id.

These eight tools are the complete public Client surface. Ordinary additional
UE behavior belongs in SAL and its interface cards rather than parallel tools.
`python` is the explicit escape hatch for capabilities already served well by
Unreal Python; it has no dry run, rollback, safe cancellation, or idempotency.
`agent_skill` contains agent workflow policy and grants no new UE capability.

The `sal_schema` tool description carries the resident guide from
`@loomle/interfaces` exactly once. MCP server instructions remain empty because
clients may expand them into every tool definition. Each remaining description
stays bounded to its own tool; `agent_skill` carries only Skill discovery
metadata rather than complete instructions. Calling `sal_schema` still returns
only the active interface index or the requested static interface; it does not
repeat the resident guide.

`agent_skill` is also local. Its description contains only resident Skill
names and trigger descriptions. Calling it with an exact name returns that
Skill's `SKILL.md` and Markdown references from the self-contained Client, so
the workflow always matches the configured MCP version and needs no separate
Codex, Claude, or Copilot Skill installation. The source and packaging contract
is defined in [`../docs/MCP_AGENT_SKILLS.md`](../docs/MCP_AGENT_SKILLS.md).

Query, Patch, and Editor return the same validated canonical Result Text in the
first MCP text block. That block declares result context and Targets, then
contains ordered Object Text or ends with `no_objects`. Editor open/close
outcomes, mutation metadata, and diagnostics use later independent SAL-comment
text blocks; they are not appended to or parsed as part of the first Result
Text block. `editor({})` and explicit `context` preserve the same context result
without redundant outcome metadata.
The Target, operation, result, and error contract is defined in
[`../sal/docs/EDITOR.md`](../sal/docs/EDITOR.md).

## Project Binding And Runtime Liveness

One MCP process is one session and binds to one stable Unreal project. Call
`status({})` once before the first Loomle operation. If it reports an unbound
session, call
`project({})` to inspect the current binding and candidates, or call
`project({ projectId })` / `project({ projectRoot })` to bind or switch. A valid
offline project can remain bound while its Editor is closed. UE-backed tools
then fail quickly with project/runtime diagnostics and never fall through to a
different online project.

MCP Roots provide automatic binding only when they identify one project
unambiguously. Binding remains session-local and sticky until another explicit
`project` call. The Client resolves the bound project to its current unique
Editor runtime and verifies `rpc.health` before invocation. Runtime records are
only discovery candidates; PID or Socket existence is never online proof.

The complete state, identity, timeout, shutdown, and multi-project contract is
defined in
[`../docs/PROJECT_BINDING_AND_RUNTIME_LIVENESS.md`](../docs/PROJECT_BINDING_AND_RUNTIME_LIVENESS.md).
Client update discovery and platform-specific update guidance are defined in
[`../docs/CLIENT_STATUS_AND_UPDATES.md`](../docs/CLIENT_STATUS_AND_UPDATES.md).
Published Clients report their `github`, `fab`, `mcp_registry`, or `claude`
distribution. That packaging identity selects only the matching update
authority. Agent-channel Clients additionally report the exact matching GitHub
Bridge release for the installed UE version; they never install it. An
unpackaged checkout reports `development` and performs no public update check.
Codex users follow the GitHub distribution; Loomle does not maintain a separate
Codex marketplace version.

## Build And Test

Node.js 20 or newer is required for development. The published platform
program embeds the exact Node.js 24 LTS runtime pinned by the packaging layer.

```bash
npm ci
npm test --workspace @loomle/client
```

`npm test` builds the shared SAL library and UE interface catalog, type-checks
the Client, produces the self-contained `client/dist/main.cjs` bundle, and runs
both source-level and isolated-bundle tests. Test code is emitted separately
under `.tmp/client-tests/`.

Run the stdio MCP server from a checkout with:

```bash
node client/dist/main.cjs mcp
```

From the repository root, build and test the currently accepted native program:

```bash
npm run build:executable
npm run test:executable
```

The accepted executable targets are `darwin-arm64` and `win32-x64`. Fab
assembly and release signing are separate 0.7 release work. Client behavior is
covered by focused component tests and the same-commit packaged UE Automation
and end-to-end release gates.
