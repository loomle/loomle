# LOOMLE Repository Structure

## 1. Objective

Define one clear structure for three different shapes of LOOMLE:

1. Source repository layout
2. Release bundle layout
3. Installed user-project layout

These three shapes are related, but they are not the same thing and must not be mixed.

## 2. Core Layering

LOOMLE should be organized into four stable layers:

1. Engine runtime layer
- Unreal plugin and Unreal-side graph/runtime logic

2. Protocol/runtime layer
- MCP server, Rust clients, schemas, protocol examples

3. Workspace layer
- Agent-facing project-local content installed into the user's project under `Loomle/`

4. Developer/release layer
- Tooling, tests, packaging, documentation

## 3. Source Repository Layout

Target top-level layout:

```text
loomle/
  engine/
    LoomleBridge/
      Source/
      Resources/
      LoomleBridge.uplugin

  mcp/
    server/
      Cargo.toml
      src/
    client/
      Cargo.toml
      src/
    protocol/
      schemas/
      examples/

  workspace/
    Loomle/
      README.md
      client/
      workflows/
      examples/
      runtime/

  tools/
    dev/
    scripts/
    smoke/
    regression/

  tests/
    unit/
    integration/
    e2e/
    fixtures/

  packaging/
    bootstrap/
    install/
    bundle/
    manifests/
    release/

  docs/
    ARCHITECTURE.md
    REPO_STRUCTURE.md
    RPC_INTERFACE.md
    MCP_PROTOCOL.md
    issues/

  README.md
```

## 4. Responsibility by Directory

### `engine/`

Contains Unreal plugin source only.

Owns:
- `LoomleBridge` plugin code
- Blueprint/Material/PCG graph logic
- Unreal-side RPC listener and graph mutation/layout behavior

Does not own:
- Rust binaries
- user workspace templates
- packaging scripts
- developer validation scripts

### `mcp/`

Contains Rust-side runtime and protocol-facing code.

`mcp/server/`
- MCP lifecycle
- tool schema validation
- runtime tool routing

`mcp/client/`
- Rust client implementations for LOOMLE entrypoints
- includes the project-local client installed under `Loomle/`
- may also include or share code with the machine-level bootstrap/install CLI

`mcp/protocol/`
- schemas
- examples
- transport-neutral payload references

### `workspace/`

Contains the source template for the user-project `Loomle/` directory.

This is not development runtime code. It is installable workspace content.

`workspace/Loomle/` should hold:
- one Agent-facing README entrypoint
- the project-local `loomle` client
- local examples
- graph-editing workflows
- machine-written runtime/config state

### `tools/`

Contains developer-facing scripts only.

Examples:
- local dev sync scripts
- smoke and regression entrypoints
- release helper scripts
- perf scripts

Nothing under `tools/` should be required in a user's installed project unless packaging explicitly copies it elsewhere.

### `tests/`

Contains formal tests and fixtures.

Examples:
- protocol tests
- integration tests
- Unreal end-to-end fixtures
- graph layout fixtures

### `packaging/`

Owns release assembly and installation rules.

Examples:
- how a first-run machine acquires the `loomle` command
- what gets copied into `Plugins/LoomleBridge`
- what gets copied into `Loomle/`
- release manifests
- bundle assembly scripts

### `docs/`

Contains repository documentation only.

Important distinction:
- `docs/` explains how LOOMLE is built and maintained
- `workspace/Loomle/README.md` explains how LOOMLE is used inside a user's project

## 5. Release Bundle Layout

Release bundles should be assembled from the source repository into a clean product shape:

```text
release/
  plugin/
    LoomleBridge/
      ...
  mcp/
    server/
      darwin/loomle_mcp_server
      linux/loomle_mcp_server
      windows/loomle_mcp_server.exe
    client/
      darwin/loomle
      linux/loomle
      windows/loomle.exe
  workspace/
    Loomle/
      ...
  manifest.json
```

This release layout is an assembly product. It does not need to match the source repository layout one-to-one.

Bootstrap assets may also be published separately:

```text
bootstrap/
  darwin/loomle
  linux/loomle
  windows/loomle.exe
```

## 6. Installed User-Project Layout

After one-time installation, the user project should look like this:

```text
MyProject/
  Plugins/
    LoomleBridge/
      Binaries/
      Resources/
      Tools/
        mcp/
          darwin/loomle_mcp_server
          linux/loomle_mcp_server
          windows/loomle_mcp_server.exe

  Loomle/
    README.md
    client/
      loomle(.exe)
    workflows/
    examples/
    runtime/
```

## 7. Runtime Placement Rules

### MCP server placement

The MCP server belongs with the Unreal plugin:

```text
Plugins/LoomleBridge/Tools/mcp/<platform>/loomle_mcp_server(.exe)
```

Reason:
- it is bridge-coupled runtime infrastructure
- it version-locks naturally with the plugin
- it should ship with Unreal-side capabilities

### Client placement

The Rust client belongs in the project-local `Loomle/` directory:

```text
Loomle/client/loomle(.exe)
```

Reason:
- it is the project-local user/agent entrypoint
- it belongs to the workspace layer, not the plugin layer
- it should evolve with project-local workflows and examples

## 8. Single-Install Product Contract

LOOMLE should install in one step and deliver all of the following:

1. Unreal plugin under `Plugins/LoomleBridge/`
2. MCP server under `Plugins/LoomleBridge/Tools/mcp/...`
3. project-local `Loomle/` workspace directory
4. Rust client under `Loomle/client/`

Users should not have to install a separate skill repository before LOOMLE becomes usable.

The installed workspace should be product-shaped, not repository-shaped:

- one entry README
- one client entrypoint
- workflow docs
- small examples
- runtime state generated by installers and diagnostics

## 9. Boundary Rules

1. Source layout must not leak directly into installed layout.
2. The plugin must not own workspace content.
3. The workspace must not own Unreal runtime code.
4. Packaging owns copy/install rules.
5. Docs for maintainers live in `docs/`; docs for installed users live under `workspace/Loomle/`.

## 10. Migration Mapping from Current Repository

Current repository areas should map like this:

```text
Current                         -> Target
Source/LoomleBridge             -> engine/LoomleBridge/Source
LoomleBridge.uplugin            -> engine/LoomleBridge/LoomleBridge.uplugin
Config/                         -> engine/LoomleBridge/Config
mcp_server/                     -> mcp/server/
tools/test_bridge_*.py          -> tools/regression/ or tests/e2e/
tools/perf_*.py                 -> tools/scripts/ or tools/dev/
docs/*                          -> docs/*
external skill repo content     -> workspace/Loomle/*
```

## 11. First Implementation Principle

Do not start by moving everything physically at once.

Start in this order:

1. freeze this target structure and install contract
2. create the new directories in the source repo
3. teach packaging to emit the new installed layout
4. move code gradually behind stable entrypoints
5. remove legacy structure only after packaging and tests are green

This keeps the migration incremental instead of disruptive.
