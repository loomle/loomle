# Loomle Bridge Packaging

Native packaging assembles the UE Bridge source and one matching standalone
TypeScript Client executable into a platform-specific QA fragment. The
assembler does not build the Client or Unreal binaries. Its only Client input
is the canonical executable produced and tested by `packaging/client`:

```text
.tmp/client/<platform-arch>/loomle(.exe)
.tmp/client/<platform-arch>/build.json
```

The adjacent schema-version-3 build receipt binds that executable to the
current product version, Client–Bridge protocol version, native target, pinned
Node runtime archive, and SHA-256. It is verified during assembly but is not
copied into the plugin. Local QA must still build and test the Client
immediately before assembly because the receipt does not fingerprint the
entire source checkout.

The staged plugin is assembled from:

- `engine/LoomleBridge` for the Unreal source plugin;
- `.tmp/client/<platform-arch>/loomle(.exe)` for the standalone Client;
- `packaging/fab/FAB_PLUGIN_README.md` for the packaged README.

## Versioned Engine Package Contract

Loomle publishes one source artifact and one complete artifact per supported
Unreal Engine version:

```text
loomle-bridge-<version>-source.zip
loomle-bridge-<version>-ue5.7.zip
loomle-bridge-<version>-ue5.8.zip
```

The source ZIP is the single Fab Project File Link. It contains the complete
Bridge `Source/`, both native Clients, and no UE-generated build output. The
complete ZIPs are the GitHub installation packages. Each retains every
source-package file and adds both Bridge binaries compiled by the matching UE
version under `Binaries/Mac` and `Binaries/Win64`.

Assembly normalizes release text files to LF before native compilation and
archiving. This prevents a persistent Windows checkout from creating
platform-specific source bytes. The Client and all other binary resources are
excluded from line-ending normalization.

Mac and Windows workflows still produce platform-specific source and
BuildPlugin trees because compilation and packaged end-to-end tests must run
on their native platforms. Those trees are internal QA fragments, not public
packages. `packaging/fab/verify-derivation.mjs` verifies each native compiled
fragment derives from its matching source. The cross-platform merger then:

- requires both fragment pairs to come from the same verified commit;
- rejects any shared source, metadata, README, license, or notice drift,
  accepting only CRLF/LF-equivalent historical text and emitting LF;
- preserves both Client and Bridge binaries byte-for-byte;
- restores a descriptor that allows exactly Mac and Win64;
- emits a source tree without `Binaries/`, `Intermediate/`, or `Saved/`;
- emits one complete tree whose only source-package additions are
  `Binaries/Mac` and `Binaries/Win64`.

`engine/LoomleBridge/Source/LoomleBridge/Private/Tests` is development input,
not release source. The assembler excludes that exact subtree before
BuildPlugin can run UHT or compile the staged plugin. This includes reflected
test headers and their generated-code inputs. The descriptor must contain
exactly one runtime module, `LoomleBridge`; a test module or any other extra
module is rejected.

Each native Client is copied to
`LoomleBridge/Resources/Loomle/<platform-arch>/loomle(.exe)`. No alternative
Client implementation or resource tree is consumed. The merged package
contains both accepted target directories. It includes an empty `Content/`
directory while keeping `CanContainContent=false`; no Unreal asset is invented
merely to retain it. It also includes Loomle's `LICENSE` and a generated
`THIRD_PARTY_NOTICES.txt` covering the pinned Node runtime and bundled
production dependencies.

Accepted native QA targets are `darwin-arm64` and `win32-x64`. Assembly narrows
the derived descriptor to Mac or Win64 respectively, narrows the module
`PlatformAllowList` to the same platform, and removes any module
`PlatformArchitectureAllowList`. UE builds a universal Mac Editor with the
runtime architecture token `MULTI`; an `arm64` module allow-list would silently
skip Loomle even while that Editor is running its arm64 slice. Architecture is
therefore an artifact property enforced by native Client tests, BuildPlugin,
and native binary audits. Mac artifacts must be arm64-only and Windows
artifacts must be PE32+ AMD64. The source descriptor remains multi-platform
development input. Accepting a QA target does not by itself advertise a release
target.

Build and test the canonical Client before assembling the Fab plugin:

```sh
npm run build:executable
npm run test:executable
```

Then assemble one target:

```bash
node packaging/fab/assemble.mjs \
  --repo-root . \
  --output-dir /tmp/loomle-fab-package \
  --target darwin-arm64
```

The assembler staging tree contains:

```text
LoomleBridge/LoomleBridge.uplugin
LoomleBridge/README.md
LoomleBridge/LICENSE
LoomleBridge/THIRD_PARTY_NOTICES.txt
LoomleBridge/Content/
LoomleBridge/Config/FilterPlugin.ini
LoomleBridge/Source/LoomleBridge/LoomleBridge.Build.cs
LoomleBridge/Resources/Loomle/<platform-arch>/loomle(.exe)
LoomleBridge/Resources/AgentSkills/format-unreal-blueprints/SKILL.md
```

Before UE compilation it must not include:

```text
LoomleBridge/Binaries/
LoomleBridge/Intermediate/
LoomleBridge/Saved/
LoomleBridge/Source/LoomleBridge/Private/Tests/
```

Platform binaries and Unreal build outputs are rejected everywhere except for
the one exact Client executable path. Before copying, the assembler verifies
all generated product/protocol artifacts, then checks the staged descriptor and
Client receipt against the root product and protocol versions. The receipt
SHA-256 is checked against both source and staged Client bytes, target fields
are checked against the accepted target, and `FilterPlugin.ini` must explicitly
keep itself, `Resources/Loomle`, `Resources/AgentSkills`, `LICENSE`, and
`THIRD_PARTY_NOTICES.txt`. Assembly also validates the copied Node license and
generates deterministic third-party notices from the production dependency
set in `package-lock.json`.

Agent Skills are authored once under the repository-root `skills/` directory
using the vendor-neutral Agent Skills format. Assembly copies that canonical
tree into `Resources/AgentSkills`; each immediate child must be a directory
with a non-empty `SKILL.md`. The packaged skill therefore travels with Loomle
without making the source format specific to Codex, Claude, or another host.
The same canonical Markdown is generated into the self-contained Client, whose
model-controlled `agent_skill` MCP tool publishes trigger metadata and loads a
matching workflow without a second host-specific Skill installation. Client
tests compare every embedded Markdown file with its repository source, while
assembly and derivation tests preserve the visible package copy.

UE BuildPlugin consumes that same staging tree and produces the full GitHub
plugin. The resulting tree must add the matching Mac dylib or Win64 DLL, mark
the descriptor `Installed=true`, retain `Config/FilterPlugin.ini`, and preserve
the exact source and Client. Both the BuildPlugin output and its final ZIP are
audited again for the one-module descriptor and the absence of test source,
`Intermediate/`, `Saved/`, and files below the empty `Content/` directory.
Because archive tools cannot infer an empty directory that BuildPlugin omitted,
release staging restores it before the final audit and ZIP creation. For
`darwin-arm64`, both binaries are arm64-only; for `win32-x64`, both use the PE
AMD64 machine type.

Run the assembler tests locally:

```sh
node --test packaging/fab/assemble.test.mjs
```

Release automation must run the Client build and executable smoke test on a
native target runner before invoking the assembler. UE BuildPlugin then compiles
that staged source for the same architecture. The pre-build staging ZIP is the
Fab source candidate; the BuildPlugin ZIP is the GitHub full-package candidate.
The assembler deliberately has no fallback input path, and the derivation audit
proves that the second package only adds UE build output to the first.

Each native verification workflow uploads two internal QA fragments:

```text
loomle-fab-source-ue5.7-<platform-arch>.zip
loomle-fab-plugin-ue<engine-version>-<platform-arch>.zip
```

All have SHA-256 sidecars and remain inputs to promotion only. Promotion
extracts, validates, and mechanically combines them. The public GitHub Release
contains the source archive, both engine-specific archives, and their sidecars:

```text
loomle-bridge-<version>-source.zip
loomle-bridge-<version>-ue5.7.zip
loomle-bridge-<version>-ue5.8.zip
```

Final releases additionally expose byte-identical stable download aliases:

```text
loomle-bridge-source.zip
loomle-bridge-ue5.7.zip
loomle-bridge-ue5.8.zip
```

These aliases exist so documentation can use GitHub's
`releases/latest/download/...` route. Fab Project File Links remain pinned to
the immutable `loomle-bridge-<version>-source.zip` asset.
