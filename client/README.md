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

The Client exposes only four MCP tools:

- `sal_query`: parse, validate, execute, and format SAL Query Text;
- `sal_patch`: parse, validate, execute, and format SAL Patch Text;
- `sal_schema`: expose the resident SAL guide, then return the active interface
  index or one static interface when called;
- `editor_context`: discover the user's current UE interaction target.

This is the implemented SAL and Context surface for the current migration
step, not the final 0.7 inventory of retained non-SAL utilities. The existing
`execute` fallback and the confirmed log, diagnostic-open, focus, and
screenshot capabilities still require explicit TypeScript Client adapters.
`jobs` is intentionally not being migrated.

The `sal_schema` tool description carries the resident guide from
`@loomle/interfaces` exactly once. MCP server instructions remain empty because
clients may expand them into every tool definition. The other three tool
descriptions stay short and specific to routing. Calling `sal_schema` still
returns only the active interface index or the requested static interface; it
does not repeat the resident guide.

Query, Patch, and Editor Context results share the same validated ordered Object
Text. Mutation execution fields and diagnostics follow that Object Text inside
ordinary SAL comments, so the complete MCP text remains directly readable and
copyable without a parallel JSON payload.

## Runtime Selection

The Client reads live Bridge records from `~/.loomle/state/runtimes` and never
guesses between multiple matching Editors. Selection order is:

1. `LOOMLE_RUNTIME_ID`;
2. `LOOMLE_PROJECT_ROOT`;
3. the runtime whose project contains the Client working directory;
4. the only online runtime.

If more than one runtime still matches, configure one of the environment
variables for that MCP server. A new connection checks that the Bridge supports
`sal.query`, `sal.patch`, and `editor.context` before invoking a tool. Lost
requests are not replayed automatically because a Patch may already have
reached UE.

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
Fab assembly, and release signing are separate 0.7 release work. The existing
`install.sh`, `install.ps1`, Cargo-based workflows, and legacy UE smoke tests
still describe the frozen 0.6 distribution and must not be treated as
validation of this Client.
