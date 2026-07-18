# Loomle 0.7 Repository And Distribution

## Goal

Loomle 0.7 has four product components:

- `sal/`: the Structured Agent Language, parser, schemas, and TypeScript library;
- `interfaces/`: Loomle's static UE interface guide and catalog;
- `client/`: the standalone MCP Client that presents agent-facing tools;
- `engine/LoomleBridge/`: the Unreal Engine plugin that performs UE work.

These components have different runtime boundaries and remain separate at the
repository root. Packaging combines them only when producing release artifacts.

## Repository Boundary

```text
loomle/
  sal/
  interfaces/
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

`sal/`, `interfaces/`, and `client/` are npm workspaces controlled by the root
`package.json` and lockfile. `engine/LoomleBridge/` follows Unreal's plugin
layout.

`sal/` owns generic language mechanics and a catalog-injection contract. Its
schema discovery has no built-in Loomle interface catalog. `interfaces/` owns
the resident guide, interface descriptions, and the static Asset, Blueprint,
Class, Graph, and Widget cards. `client/` composes both packages for MCP.

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

Production type checking contains only `src/`. The production build then uses
that single entry point to emit only `client/dist/main.cjs`. Test compilation is
separate and emits temporary files outside `client/dist/`. This keeps tests out
of the release input while making the exact bundled artifact part of normal
verification.

## Artifact Contract

The release path is:

```text
SAL + Interfaces + Client source
  -> client/dist/main.cjs
  -> .tmp/client/<platform-arch>/loomle(.exe)
  -> staged LoomleBridge Fab plugin
```

`client/dist/main.cjs` is one platform-neutral Node.js bundle. It contains the
Client, SAL, Interfaces, and all non-built-in runtime dependencies. It must run
without the repository or `node_modules`; only Node built-in modules remain
external. CommonJS gives this intermediate the module format required by the
planned Node 20 single-executable path. Actual SEA compatibility is accepted
only after building and running the platform program with its selected Node
runtime.

The only platform program path consumed by Fab packaging, release workflows,
and executable tests is:

```text
.tmp/client/<platform-arch>/loomle(.exe)
```

The future `packaging/client/` implementation owns how that executable is
built. Callers must not infer Cargo `target/` paths, npm internals, or another
private build location.

## Embedded Static Resources

The standalone program must not depend on repository-relative files at
runtime. `interfaces/` generates its guide and static cards into a TypeScript
catalog consumed by the Client. SAL likewise generates its canonical JSON
Schema into an internal TypeScript text module used by result validation.
Static resources carried into the final Client are therefore:

- the generated interface catalog;
- the generated runtime text of the normalized SAL JSON Schema;
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

The root `package.json` is the only manually maintained product-version source.
`sal/`, `interfaces/`, and `client/` are private implementation workspaces and
keep the non-product package version `0.0.0`.

The product-version generator derives and checks two runtime values:

- the Client's generated MCP Server version module;
- `LoomleBridge.uplugin` `VersionName`.

The root entries in `package-lock.json` are npm-managed mirrors of the same
source. A release or local build must fail when any derived value is stale.
Fab staging must verify the copied plugin descriptor again rather than invent
another version source.

The following values are deliberately independent:

- RPC protocol and runtime schema versions change only with their contracts;
- `LoomleBridge.uplugin` `FileVersion` follows the UE descriptor format;
- `LoomleBridge.uplugin` integer `Version` is a monotonic Fab build number;
- `LoomleBridge.uplugin` `EngineVersion` states the UE compatibility target.

Changing the product version therefore never increments the Fab build number
or changes protocol compatibility. Change it with npm without creating a tag,
then regenerate and verify the derived values:

```sh
npm version <version> --no-git-tag-version
npm run generate:version
npm test
```

The `--no-git-tag-version` flag is required while the old tag-triggered 0.6
workflows remain in the repository.

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

1. Establish clean SAL, Interfaces, and Client workspace boundaries.
2. Separate Client production and test compilation.
3. Inject the static UE interface catalog into generic SAL discovery.
4. Centralize product-version injection and embed the SAL runtime schema.
5. Build one platform-neutral Client bundle.
6. Produce the standalone program at the canonical artifact path.
7. Assemble the program into the Fab plugin.
8. Replace release and verification workflows.
9. Retire the Python MCP, Cargo paths, and website bootstrap chain.

Each increment must preserve workspace tests and must not activate tag-driven
release workflows before the complete 0.7 artifact path exists.
