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
subdirectories should be introduced only when a concrete module becomes too
large or mixes unrelated responsibilities.

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

The only platform program paths consumed by Fab packaging, verification
workflows, and executable tests are:

```text
.tmp/client/darwin-arm64/loomle
.tmp/client/win32-x64/loomle.exe
```

`packaging/client/` owns how each executable is built. Accepted native QA
targets are `darwin-arm64` and `win32-x64`. Callers consume only the canonical
artifact path and must not infer another private build location. A QA target
does not become an advertised release target merely by existing here.

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
  Content/
  LICENSE
  THIRD_PARTY_NOTICES.txt
  Source/
  Resources/
    Loomle/
      <platform-arch>/
        loomle(.exe)
  LoomleBridge.uplugin
```

The Fab assembler accepts only the canonical standalone Client program and has
no alternative Client layout or script fallback.

The Client discovers live Bridge records through `~/.loomle/state/runtimes`.
That directory is runtime state, not a global Loomle installation and not a
project-local Client copy. Each record advertises the numeric Client–Bridge
`protocolVersion`. A missing or different version is incompatible and is
rejected before tool dispatch. A matching record is only an early discovery
check; the live `rpc.capabilities` response must report the same version before
the Client invokes any Bridge tool.

Fab assembly takes an explicit native target and has no fallback input:

```sh
npm run build:executable -- --target darwin-arm64
npm run test:executable -- --target darwin-arm64
npm run assemble:fab -- \
  --output-dir .tmp/fab/darwin-arm64 \
  --target darwin-arm64
```

The executable build writes an adjacent schema-version-3 `build.json` receipt
containing its product and Client–Bridge protocol versions, target, Node
version and runtime archive SHA-256, executable name and SHA-256, and the
SHA-256 of the copied Node runtime license.
The assembler verifies the repository's generated version artifacts, that
receipt, and the Client bytes, but does not ship the receipt. It rejects a
missing or receipt-mismatched canonical Client, product- or protocol-version
drift, another staged Client target, unexpected Client resources, and platform
build outputs. The receipt does not fingerprint every source file, so both
local QA and automation build and test the Client immediately before assembly.
For `darwin-arm64` it narrows the derived descriptor to Mac; for `win32-x64`
it narrows it to Win64. Both deliberately omit a module architecture
allow-list. UE represents a universal Mac Editor as architecture `MULTI`, even
when its active process slice is arm64, so `Mac:arm64` would prevent the module
from loading. The source descriptor remains multi-platform development input.
UE BuildPlugin must compile the matching target, native audits must prove the
requested Client and Bridge architecture, and `Config/FilterPlugin.ini` must
preserve the exact Client bytes. The final QA or release archive is always the
BuildPlugin output, never the pre-build staging tree. It must contain the
matching Bridge binary, `Installed=true` descriptor, retained filter contract,
and the same one-target Client payload. Before BuildPlugin runs UHT, Fab
assembly removes the development-only
`Source/LoomleBridge/Private/Tests` subtree and requires the descriptor to
contain exactly one module named `LoomleBridge`. The BuildPlugin output and
final ZIP repeat that audit and must not contain test source, `Intermediate/`,
`Saved/`, or files below `Content/`. The distributable intentionally includes
an empty `Content/` directory, even though `CanContainContent=false`, together
with Loomle's `LICENSE` and a generated `THIRD_PARTY_NOTICES.txt`. The notices
are generated from the pinned Node runtime and the production packages in the
root lockfile; missing license text fails assembly.

## Host Setup

The Bridge resolves exactly one Client path for its current compiled target:

```text
<Plugin>/Resources/Loomle/<node-platform>-<arch>/loomle(.exe)
```

The platform spelling follows the Client build target (`darwin`, `win32`, or
`linux`) rather than UE display names. Architecture comes from the current UE
process target, not the host machine. There is no runtime fallback to
another plugin or user-directory location.

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

The root `package.json` is the only manually maintained repository version
source. Its top-level `version` is the product version, while
`loomle.protocolVersion` is the independent positive integer for the private
Client–Bridge contract. `sal/`, `interfaces/`, and `client/` are private
implementation workspaces and keep the non-product package version `0.0.0`.
The incompatible 0.7 contract uses version `2`; version `1` belongs to the 0.6
Client–Bridge contract and must never be treated as compatible by tool-name
overlap.

The version generator derives and checks four runtime values:

- the Client's generated MCP Server version module;
- `LoomleBridge.uplugin` `VersionName`;
- the Client's generated protocol-version module;
- the Bridge's generated protocol-version header.

The root entries in `package-lock.json` are npm-managed mirrors of the same
source. A release or local build must fail when any derived value is stale.
Fab staging must verify the copied plugin descriptor again rather than invent
another version source.

The following values are deliberately independent:

- Client–Bridge protocol and runtime-record schema versions change only with
  their contracts;
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

The `--no-git-tag-version` flag remains required. Version preparation must
never implicitly start a release; promotion is a separate manual workflow that
derives its tag from the already-tested commit.

## 0.6 History And Upgrade

The `0.6` branch and its release tags preserve the 0.6 implementation and
documents. They are not copied into `main` as inactive alternatives. The 0.7
tree contains one TypeScript Client, one SAL contract, and one Fab packaging
path.

Before a project moves from 0.6 to the Fab-installed 0.7 plugin, its old
`<Project>/Plugins/LoomleBridge` copy must be backed up if modified and removed
or moved outside `Plugins`. UE gives a same-named project plugin precedence over
the engine plugin, so the old Bridge would otherwise shadow 0.7. Fab cannot
perform this project-file migration, and the shadowed 0.7 plugin cannot detect
it from inside that project.

The Mac and Windows/Fab workflows are manual QA only. They have read-only
repository permission, upload audited ZIPs and durable result files, and never
create a tag, GitHub Release, latest alias, or public Fab submission. The
separate manual promotion workflow currently consumes only one successful Mac
run and may publish its exact ZIP as a GitHub prerelease. It cannot build,
sign, or recompress the candidate. Windows QA is intentionally independent:
passing it proves the native implementation path, but does not yet add Windows
to release notes or promotion.

## Verification Boundary

Verification is layered. SAL, Interfaces, Client, packaging, and Bridge keep
focused tests beside their implementation, and the root npm suite composes the
fast TypeScript and packaging checks. A same-commit test-bearing BuildPlugin
candidate runs the complete Loomle UE Automation category. A separate stripped
release candidate is then built, audited, archived, and exercised through the
small packaged Client-to-UE workflow against that exact ZIP. See
`TESTING_AND_RELEASE_GATES.md` for the runner and promotion contract.

Tag-driven builds are not part of the 0.7 design. Promotion is a final explicit
manual step after every advertised artifact has passed the gates for its
channel. Unsigned `0.7.0-rc.*` prereleases are allowed only with an explicit
Gatekeeper warning. Stable promotion remains disabled until Developer ID
signing and notarization occur before packaged end-to-end verification.
