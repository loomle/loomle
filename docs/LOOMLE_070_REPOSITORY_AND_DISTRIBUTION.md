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
external. CommonJS gives this intermediate the module format required by
Node's single-executable application path. Executable construction pins an
exact Node.js 24 LTS runtime and verifies its official archive checksum. SEA
compatibility is accepted per platform only after building and running the
final platform program.

The only platform program path consumed by Fab packaging, release workflows,
and executable tests is:

```text
.tmp/client/<platform-arch>/loomle(.exe)
```

`packaging/client/` owns how that executable is built. Its first accepted
target is `darwin-arm64`; other targets are not implied until they pass the
same native executable tests. Callers must not infer Cargo `target/` paths, npm
internals, or another private build location.

```sh
npm run build:executable
npm run test:executable
```

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
layout. The 0.7 Fab assembler accepts only the canonical standalone Client
program and has no script or legacy-server fallback.

The Client discovers live Bridge records through `~/.loomle/state/runtimes`.
That directory is runtime state, not a global Loomle installation and not a
project-local Client copy.

Fab assembly takes an explicit native target and has no fallback input:

```sh
npm run build:executable -- --target darwin-arm64
npm run test:executable -- --target darwin-arm64
npm run assemble:fab -- \
  --output-dir .tmp/fab/darwin-arm64 \
  --target darwin-arm64
```

The executable build writes an adjacent `build.json` receipt containing its
product version, target, Node version and runtime archive SHA-256, executable
name, and executable SHA-256. The assembler verifies that receipt and the
Client bytes, but does not ship the receipt. It rejects a missing or
receipt-mismatched canonical Client, product-version drift, `Resources/MCP`,
another staged Client target, and
unexpected platform build outputs. The receipt does not fingerprint every
source file, so both local QA and automation build and test the Client
immediately before assembly. For the accepted `darwin-arm64` target it also
narrows the derived descriptor to Mac and `Mac:arm64`; the source descriptor
remains development input for later targets. UE BuildPlugin must compile the
same single architecture and preserve the exact Client bytes through
`Config/FilterPlugin.ini`. The final QA or release archive is always the
BuildPlugin output, never the pre-build staging tree. It must contain the
matching Bridge binary, `Installed=true` descriptor, retained filter contract,
and the same one-target Client payload.

## Host Setup

The Bridge resolves exactly one Client path for its current compiled target:

```text
<Plugin>/Resources/Loomle/<node-platform>-<arch>/loomle(.exe)
```

The platform spelling follows the Client build target (`darwin`, `win32`, or
`linux`) rather than UE display names. Architecture comes from the current UE
process target, not the host machine. There is no runtime fallback to
`Resources/MCP` or `~/.loomle/bin/loomle`.

Codex and Claude Desktop configuration use the absolute bundled Client as
`command` with `args = ["mcp"]`. The Bridge status panel is read-only with
respect to host configuration: it detects the current entry, classifies exact
bundled, recognized 0.6, stale, manual, missing, and ambiguous states, then
provides copyable setup or migration guidance. It never writes Codex or Claude
Desktop configuration files.

This boundary is intentional. UE's cross-platform file layer cannot by itself
guarantee cross-process compare-and-swap, permission/ACL preservation, and
atomic replacement while several Editors or the MCP host may be active. A
future one-click writer therefore requires a separately designed native helper
with those guarantees; it must not be reintroduced as an in-process best-effort
write. Codex detection respects `CODEX_HOME` and otherwise uses `~/.codex`.
Malformed, ambiguous, custom, unreadable, or concurrently changing host state
is reported without modification. A valid bundled Client entry from another
engine is reported as current so multiple engines do not compete to rewrite one
host configuration.

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

The `--no-git-tag-version` flag remains required until the final manual 0.7
promotion workflow is designed and enabled. Creating a version must never
implicitly start a release.

## Legacy Retirement

The following 0.6 mechanisms are not migrated into the Client:

- Rust Client and Cargo `target/` output;
- Python MCP and `Resources/MCP`;
- global bundle installation and `project.install`;
- the old release manifest and plugin-cache workflow.

The 0.6 branch remains the maintenance source for its Python MCP, global
installers, manifests, plugin cache, and tag-driven release workflow. Those
implementations are not retained as inactive alternatives on `main`; historical
design documents live under `docs/archive/legacy/0.6/`.

Before a project moves from 0.6 to the Fab-installed 0.7 plugin, its old
`<Project>/Plugins/LoomleBridge` copy must be backed up if modified and removed
or moved outside `Plugins`. UE gives a same-named project plugin precedence over
the engine plugin, so the old Bridge would otherwise shadow 0.7. Fab cannot
perform this project-file migration, and the shadowed 0.7 plugin cannot detect
it from inside that project.

The active Mac/Fab workflow is manual QA only. It has read-only repository
permission, uploads an Actions artifact, and never creates a tag, GitHub
Release, latest alias, or public Fab submission. Windows and formal release
promotion remain unavailable until their native Client, signing, packaging,
and verification paths satisfy the same contract.

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

Each increment must preserve workspace tests. Tag-driven release workflows are
not part of the 0.7 design; formal release promotion is a final explicit manual
step after every advertised artifact has already passed signing and
verification.
