# LOOMLE Repository Structure

## 1. Objective

Define one clear structure for three related but different shapes of LOOMLE:

1. source repository layout
2. release bundle layout
3. installed machine/project layout

These shapes must not be mixed.

`0.4.0` should organize them around product responsibilities, not around the
current implementation language split.

## 2. Core Layering

`LOOMLE 0.4.0` should be organized into four stable layers:

1. `cli/`
- global machine-level entrypoint and agent-facing protocol client/launcher

2. `global/`
- reusable machine-level LOOMLE capabilities shared across projects

3. `project/`
- content that is installed into or initialized inside a specific Unreal
  project

4. `engine/`
- Unreal integration and project runtime authority

This replaces the older mental model centered on:

- `mcp/client`
- `mcp/server`
- `workspace/Loomle`

That older structure reflected an earlier implementation stage, not the target
`0.4.0` product boundary.

## 3. Target Source Repository Layout

Target top-level layout:

```text
loomle/
  cli/
    loomle/
    installer/
    protocol/

  global/
    skills/
    guides/
    references/
    templates/

  project/
    template/
      loomle/
      .loomle-core/

  engine/
    LoomleBridge/
      Source/
      Resources/
      LoomleBridge.uplugin
    python/
      server/
      runtime/
      tools/

  packaging/
    bootstrap/
    install/
    manifests/
    release/

  tests/
    cli/
    runtime/
    e2e/
    fixtures/

  docs/
    ARCHITECTURE.md
    REPO_STRUCTURE.md
    LOOMLE_040_PRODUCT_DIRECTION.md
    LOOMLE_040_STRUCTURE_REFACTOR.md
    LOOMLE_040_INSTALL_UPGRADE_DESIGN.md
    LOOMLE_040_RUNTIME_CONNECTIVITY.md
    RPC_INTERFACE.md
    MCP_PROTOCOL.md
    issues/

  README.md
```

## 4. Responsibility by Directory

### `cli/`

Owns the global machine-level entrypoint.

Examples:

- global `loomle` executable
- `loomle mcp` protocol entrypoint
- install/update/doctor command entrypoints
- installer handoff/update helper logic
- agent-facing stdin/stdout protocol implementation

Does not own:

- Unreal runtime authority
- project runtime server logic
- project-visible collaboration artifacts

### `global/`

Owns reusable machine-level LOOMLE capability content.

Examples:

- role skills
- workflow skills
- guides
- references
- shared templates

This directory exists because some capabilities that were previously placed
inside a project only lived there because LOOMLE did not yet have a true global
installation layer.

`0.4.0` should move those reusable capabilities back to the machine-level
layer.

### `project/`

Owns content that is initialized into a specific Unreal project.

Examples:

- visible `loomle/` template content
- hidden `.loomle-core/` template content
- project attach/init starter files

This directory should contain only project-bound initialization content.

It should not continue to carry generic cross-project capabilities that belong
in `global/`.

### `engine/`

Owns Unreal integration and runtime authority.

Examples:

- `LoomleBridge` plugin code
- editor/runtime integration
- graph logic
- project runtime server lifecycle
- Unreal-hosted Python server/runtime/tool code

This is where the project runtime server belongs.

The runtime server does not belong in `cli/`.

### `packaging/`

Owns release assembly, install, attach/init, upgrade, and manifest rules.

Examples:

- machine install rules for global `loomle`
- project attach/init rules
- component manifests
- release assembly
- bootstrap assets

### `tests/`

Owns formal test assets.

Examples:

- CLI/protocol tests
- runtime tests
- Unreal end-to-end tests
- fixtures

### `docs/`

Owns maintainer-facing repository documentation.

Important distinction:

- `docs/` explains how LOOMLE is built and maintained
- `project/template/` explains what is initialized into user projects

## 5. Runtime Placement Rules

### Global client placement

The stable global entrypoint belongs in:

```text
cli/loomle/
```

Reason:

- it is a machine-level executable
- it acts as the client/launcher
- it is the approval boundary for host execution

### Runtime server placement

The project runtime server belongs with Unreal integration:

```text
engine/
  LoomleBridge/
  python/server/
```

Reason:

- Unreal owns runtime authority
- the server is project-attached runtime infrastructure
- the global CLI should connect to it, not replace it

### Project content placement

Project-visible and project-hidden initialization content belongs in:

```text
project/template/
```

Reason:

- this content is materialized into a project
- it is not machine-level capability source

## 6. Release Bundle Layout

Release bundles should be assembled into a product shape that matches the new
layer model.

Illustrative layout:

```text
release/
  cli/
    darwin/loomle
    linux/loomle
    windows/loomle.exe

  global/
    skills/
    guides/
    references/
    templates/

  project/
    template/
      loomle/
      .loomle-core/

  engine/
    LoomleBridge/
      ...
    python/
      server/
      runtime/
      tools/

  manifest.json
```

Bootstrap/update helper assets may also be published separately:

```text
bootstrap/
  darwin/loomle-installer
  windows/loomle-installer.exe
```

## 7. Installed Shape

After install and project attach/init, the machine/project split should look
like this.

### Machine-level install

```text
<UserHome>/
  ... global LOOMLE home ...
    loomle(.exe)
    skills/
    guides/
    references/
    templates/
```

### Project-level install

```text
MyProject/
  Plugins/
    LoomleBridge/
      ...

  loomle/
    ...

  .loomle-core/
    ...
```

The exact machine install path can vary by platform. The structural rule is
more important than the literal path in this document.

## 8. Boundary Rules

1. `cli/` owns the global executable and client protocol surface.
2. `global/` owns reusable cross-project capability content.
3. `project/` owns only project-initialized content.
4. `engine/` owns Unreal integration and runtime authority.
5. `packaging/` owns how these layers are assembled and installed.
6. Source layout must not leak directly into installed layout.

## 9. Migration Mapping from Current Repository

Current repository areas should move conceptually like this:

```text
Current                         -> Target
mcp/client/                     -> cli/loomle/
mcp/server/                     -> transitional runtime area (Windows fallback only; remove after native MCP runtime cutover)
mcp/protocol/                   -> cli/protocol/ and/or docs-owned protocol assets
workspace/Loomle/               -> project/template/       (for true project-bound content)
shared project-local capabilities -> global/
engine/LoomleBridge/            -> engine/LoomleBridge/
```

Important clarification:

- not everything currently under `workspace/Loomle/` still belongs in the
  project layer
- some of that content only lived inside the project because LOOMLE lacked a
  global install layer
- `0.4.0` should split those concerns cleanly

## 10. Transitional Principle

Do not start by physically moving every directory immediately.

Start in this order:

1. freeze this target structure in docs
2. treat old `mcp/` and `workspace/` layout as transitional
3. teach packaging/install logic to emit the new machine/project structure
4. move code and assets gradually behind stable entrypoints
5. remove legacy layout only after packaging, docs, and tests agree

This keeps the migration incremental instead of disruptive.
