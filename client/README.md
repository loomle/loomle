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

The Client exposes six MCP tools:

- `status`: inspect Client identity and update availability plus the bound
  session and Bridge health;
- `project`: inspect project availability and bind this MCP session to one
  project;
- `sal_query`: parse, validate, execute, and format SAL Query Text;
- `sal_patch`: parse, validate, execute, and format SAL Patch Text;
- `sal_schema`: expose the resident SAL guide, then return the active interface
  index or one static interface when called;
- `editor_context`: discover the user's current UE interaction target.

These six tools are the complete public Client surface. Additional UE behavior
belongs in SAL and its interface cards rather than parallel compatibility
tools.

The `sal_schema` tool description carries the resident guide from
`@loomle/interfaces` exactly once. MCP server instructions remain empty because
clients may expand them into every tool definition. The other five tool
descriptions stay short and specific to their boundary. Calling `sal_schema` still
returns only the active interface index or the requested static interface; it
does not repeat the resident guide.

Query, Patch, and Editor Context results share the same validated ordered Object
Text. Mutation execution fields and diagnostics follow that Object Text inside
ordinary SAL comments, so the complete MCP text remains directly readable and
copyable without a parallel JSON payload.

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

The first accepted executable target is `darwin-arm64`. Remaining platforms,
Fab assembly, and release signing are separate 0.7 release work. Editor E2E
coverage will be designed directly against SAL; current verification consists
of the focused component tests and UE BuildPlugin compilation.
