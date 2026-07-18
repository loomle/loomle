# Loomle 0.7 Repository And Distribution

## Goal

Loomle 0.7 has three product components:

- `sal/`: the Structured Agent Language, parser, schemas, and SDK;
- `client/`: the standalone MCP Client that presents agent-facing tools;
- `engine/LoomleBridge/`: the Unreal Engine plugin that performs UE work.

These components have different runtime boundaries and remain separate at the
repository root. Packaging combines them only when producing release artifacts.

## Repository Boundary

```text
loomle/
  sal/
  client/
  engine/LoomleBridge/
  packaging/
    client/
    fab/
    release/
  docs/
  tests/
  tools/
  site/
```

`sal/` and `client/` are npm workspaces controlled by the root `package.json`
and lockfile. `engine/LoomleBridge/` follows Unreal's plugin layout.

`packaging/` owns executable construction, Fab assembly, versioned artifacts,
and release verification. Platform binaries and staged plugins are build
outputs and are never committed under `client/` or `engine/LoomleBridge/`.

## Client Boundary

`client/` contains only the standalone Client source and its development
support:

```text
client/
  src/
  tests/
  scripts/
  package.json
  tsconfig.json
  tsconfig.test.json
  README.md
```

The current source is intentionally flat while it remains small. New
subdirectories should be introduced only when retained tools make a concrete
module too large or mix unrelated responsibilities.

Production compilation contains only `src/` and emits `client/dist/main.js`.
Test compilation is separate and emits temporary files outside `client/dist/`.
This keeps the production input free of test code without changing how unit
tests exercise the source.

## Artifact Contract

The release path is:

```text
SAL + Client source
  -> client/dist/main.js
  -> .tmp/client/<platform-arch>/loomle(.exe)
  -> staged LoomleBridge Fab plugin
```

`client/dist/main.js` is a platform-neutral production intermediate. The only
platform program path consumed by packaging, workflows, and executable tests
is:

```text
.tmp/client/<platform-arch>/loomle(.exe)
```

The future `packaging/client/` implementation owns how that executable is
built. Callers must not infer Cargo `target/` paths, npm internals, or another
private build location.

## Embedded SAL Resources

The standalone program must not depend on repository-relative files at
runtime. Its build must embed the static SAL resources needed by the resident
guide, `sal_schema`, parsing, and result validation, including:

- the interface index and interface documents;
- the normalized SAL JSON Schema;
- any generated data required by the SAL SDK.

This keeps the published Client self-contained and avoids a parallel runtime
installation of `node_modules`, SAL documents, or schemas.

## Fab Layout

Fab is the 0.7 installation channel. The staged plugin contains both the UE
Bridge and the matching standalone Client:

```text
LoomleBridge/
  Source/
  Resources/
    Loomle/
      <platform-arch>/
        loomle(.exe)
  LoomleBridge.uplugin
```

`Resources/MCP` is a legacy Python MCP location and is not part of the 0.7
layout. If a distribution channel requires a script fallback, it still belongs
under `Resources/Loomle/`; the public product boundary does not change.

The Client discovers live Bridge records through `~/.loomle/state/runtimes`.
That directory is runtime state, not a global Loomle installation and not a
project-local Client copy.

## Version Boundary

The 0.7 release target is to use the root `package.json` version as the product
version source. Release construction must inject it into the Client and verify
the staged plugin's `VersionName`.

During the current migration, `client/package.json` and the MCP Server still
contain matching transitional values. They are not additional sources of
truth. Version injection and removal of the runtime literal must land before
the standalone executable becomes a release artifact.

RPC protocol compatibility remains independent from product versioning and
changes only when the protocol contract changes.

## Legacy Retirement

The following 0.6 mechanisms are not migrated into the Client:

- Rust Client and Cargo `target/` output;
- Python MCP and `Resources/MCP`;
- global bundle installation and `project.install`;
- the old release manifest and plugin-cache workflow.

The old `client/install.sh` and `client/install.ps1` remain temporarily because
the current website deployment copies them. They must be retired atomically
with the website bootstrap documentation and workflow, not moved into the new
Client or treated as 0.7 installers. The `0.6` branch remains their maintenance
source.

## Migration Order

1. Establish clean SAL and Client workspace boundaries.
2. Separate Client production and test compilation.
3. Centralize product-version injection and embed SAL runtime resources.
4. Build one platform-neutral Client bundle.
5. Produce the standalone program at the canonical artifact path.
6. Assemble the program into the Fab plugin.
7. Replace release and verification workflows.
8. Retire the Python MCP, Cargo paths, and website bootstrap chain.

Each increment must preserve workspace tests and must not activate tag-driven
release workflows before the complete 0.7 artifact path exists.
