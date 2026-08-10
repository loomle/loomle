# Agent Distribution Channels Design

## Status

This is the umbrella distribution design. Its native GitHub/Fab foundation and
shared agent-package assembler are implemented: the Client resolves strict
package identity, isolates update authorities, the native release path emits
separate channel metadata without changing executable bytes, and one verified
`client/dist/main.cjs` now produces Registry MCPB and Claude MCPB candidates,
plus an internal Codex compatibility package, with identical Client hashes.
The Registry promotion workflow and the strict Fab/Claude channel-document
validator are implemented; absent unpublished channel files fail safely to
`unknown`.

The source facts in this design were rechecked against the public GitHub, Fab,
MCP Registry, Claude, and OpenAI plugin documentation on August 10, 2026.

## Intent

Loomle has one product source but several independently promoted distribution
channels. GitHub is the fastest public product channel. Fab and the agent
channels may remain on older Loomle versions until their own review,
validation, or publication step is complete.

The design must provide all of the following:

- one Client implementation and one Client–Bridge protocol;
- immutable, versioned artifacts derived from a verified GitHub release;
- independent current-version pointers for GitHub, Fab, MCP Registry, and
  Claude;
- update discovery that never redirects one channel to another channel's
  Client package;
- exact versioned GitHub Bridge downloads for agent-channel users;
- no automatic installation or replacement of Unreal Engine plugins;
- failure of update discovery must never block local MCP or UE work.

## Verified Platform Boundaries

### GitHub Releases

GitHub exposes the latest published full release at:

```text
GET https://api.github.com/repos/loomle/loomle/releases/latest
```

The response includes the release tag, versioned asset download URLs, asset
sizes, and SHA-256 digests. Drafts and prereleases are excluded from this
endpoint. Loomle already validates those fields rather than trusting an
unversioned website redirect.

Loomle treats every published versioned GitHub release and asset as immutable:
the release workflow never replaces an asset under an existing tag. Those
versioned assets are the artifact origin for every Loomle channel. The GitHub
channel may advance as soon as Loomle's native verification and release
promotion gates pass.

### Fab

Fab publisher updates are performed in the Fab publishing UI. File and engine
package changes require review. The existing live version remains available
while an update is pending. Code plugins require an updated project submission
for a new engine version, and users obtain UE-format updates through the Fab
Library in the Epic Games Launcher.

Fab does not document a public product-version API that Loomle can safely use
as an in-Client update source. The Fab Client must therefore use a Loomle-owned
Fab channel pointer for discovery, while the actual acquisition and
installation remain in Fab and the Epic Games Launcher.

### MCP Registry

The official Registry stores metadata, not Loomle artifact bytes. It accepts an
MCPB hosted in a public GitHub Release and requires its SHA-256 in
`server.json`. Loomle publishes under:

```text
io.github.loomle/loomle
```

Publication uses `mcp-publisher` and GitHub OIDC in Actions. A published
Registry version and its metadata are immutable. Updating the listing means
publishing a new unique semantic version. The Registry exposes the current
version through:

```text
GET https://registry.modelcontextprotocol.io/v0.1/servers/
    io.github.loomle%2Floomle/versions/latest
```

Registry-aware hosts decide how to install or upgrade the MCPB. Loomle only
reports the available Registry version and does not modify host configuration.

### Claude MCPB

Claude accepts local MCP servers as MCP Bundles. An MCPB contains a local MCP
Server, its manifest, and its runtime dependencies. Claude Desktop supplies a
Node.js runtime on macOS and Windows, so Loomle can package its existing
self-contained `main.cjs` without the native SEA executables shipped in the UE
plugin.

The public Claude documentation defines a separate submission form for MCPB
desktop extensions. It documents automatic GitHub mirroring for Claude
plugins, and no-resubmission deployment for remote MCP servers, but it does not
currently document an equivalent public update contract for an already listed
MCPB. Loomle must not assume that replacing a GitHub asset updates the Claude
Directory.

Until Anthropic exposes a documented MCPB update interface, every Claude MCPB
promotion must be treated as a managed directory update: upload or submit the
new MCPB through the available Claude management flow, wait until it is live,
and only then advance Loomle's Claude channel pointer.

### OpenAI And Codex

ChatGPT and Codex now share one Universal Plugins Directory. OpenAI's public
submission flow is the only public store path; local and repository
marketplaces are authoring, testing, and team-distribution sources rather than
public discovery channels. Requiring users to discover Loomle elsewhere and
then add a Loomle Git marketplace does not improve product discovery enough to
justify another independently versioned channel.

Loomle therefore does not publish a `codex-marketplace` branch or maintain a
Codex channel pointer. Codex users install the GitHub distribution from the
website and follow the GitHub stable release. The assembler may continue to
generate a Codex plugin layout for compatibility tests, but that layout embeds
`LOOMLE_DISTRIBUTION_CHANNEL=github` and is not a public Release asset.

OpenAI's documented public MCP plugin flow currently requires a registered,
public production MCP endpoint. Loomle's MCP Client is intentionally local and
connects to a user's live Unreal Editor. A Universal Directory submission is
deferred until OpenAI documents acceptance of bundled local MCP servers or
explicitly approves this architecture; Loomle will not introduce a cloud relay
only to satisfy a store shape.

## Version Model

Loomle does not invent unrelated per-store version numbers. Every artifact
keeps the product version of the exact source release from which it was built.
Only the channel's current pointer is independent.

For example:

```text
GitHub current:       0.8.3
Fab current:          0.8.1
MCP Registry current: 0.8.2
Claude current:       0.8.1
```

The four channel pointers may differ, but an artifact labelled `0.8.2` must be
derived from tag `v0.8.2` and must embed Loomle product version `0.8.2`.
Rebuilding different bytes under an existing channel version is prohibited.

Product version and Client–Bridge protocol version remain separate:

- `productVersion` identifies source and release artifacts;
- `protocolVersion` determines runtime Client–Bridge compatibility;
- packaging and documentation pair a Client with the same product-version
  Bridge by default;
- runtime may accept another product version only when its exact numeric
  `protocolVersion` matches, preserving the current compatibility contract.

## Canonical Artifact Derivation

The platform-neutral Client bundle remains the single implementation input:

```text
SAL + Interfaces + Client + Agent Skills
                  |
                  v
        client/dist/main.cjs
          |        |          |          |
          v        v          v          v
      Native SEA  Registry   Claude     Codex
      Clients     MCPB       MCPB       plugin
```

The derived artifacts are:

```text
GitHub UE package
  LoomleBridge source and verified UE binaries
  native darwin-arm64 and win32-x64 Clients
  distribution channel: github

Fab source package
  LoomleBridge source
  native darwin-arm64 and win32-x64 Clients
  distribution channel: fab

MCP Registry MCPB
  manifest.json
  server/loomle.cjs
  distribution channel: mcp_registry

Claude MCPB
  manifest.json
  server/loomle.cjs
  distribution channel: claude

Codex plugin
  .codex-plugin/plugin.json
  .mcp.json
  mcp/loomle.cjs
  internal compatibility artifact only
  distribution channel: github
```

The two MCPB files may differ in manifest metadata and channel environment,
but their `server/loomle.cjs` bytes must be identical to each other and to the
verified `client/dist/main.cjs`. The internal Codex copy must have the same
hash, without becoming another public channel or separately maintained program.

## Runtime Channel Identity

Channel identity affects only update discovery and guidance. It never changes
MCP tools, SAL behavior, UE permissions, runtime discovery, or protocol
compatibility.

The accepted channel identifiers are:

```text
github
fab
mcp_registry
claude
development
```

Native packages carry an adjacent packaging-owned file:

```json
{
  "schemaVersion": 1,
  "channel": "fab"
}
```

The GitHub package carries the same file with `channel: "github"`. The file
does not repeat the product or protocol version; those remain compiled into
the Client and verified through the existing build receipt.

MCPB manifests set:

```text
LOOMLE_DISTRIBUTION_CHANNEL=mcp_registry
LOOMLE_DISTRIBUTION_CHANNEL=claude
```

The internal Codex `.mcp.json` uses `github` because Codex users follow the
GitHub release channel.

The Client resolves the channel in this order:

1. a valid packaging-provided environment value;
2. a valid adjacent native package file;
3. `development` when neither exists.

Unknown values fail closed to `development`. Channel selection cannot be used
to unlock capabilities or weaken safety checks.

## Channel Update Sources

| Channel | Discovery source | Actual update authority | Bridge source |
| --- | --- | --- | --- |
| GitHub | GitHub Releases `latest` API | GitHub Release | package already contains Bridge |
| Fab | `loomle.ai/channels/fab.json` | Fab Library / Epic Games Launcher | package installed by Fab |
| MCP Registry | official Registry `versions/latest` API | Registry-aware MCP host | exact GitHub release |
| Claude | `loomle.ai/channels/claude.json` | Claude MCPB management flow | exact GitHub release |

The Loomle-owned channel documents are small discovery pointers, not package
hosts and not update executors. They advance only after the corresponding
channel version is actually public.

## Channel Document Contract

Fab and Claude use a shared strict JSON shape:

```json
{
  "schemaVersion": 1,
  "channel": "claude",
  "version": "0.8.1",
  "publishedAt": "2026-08-10T00:00:00Z",
  "listingUrl": "https://claude.ai/directory/connectors/loomle",
  "bridge": {
    "source": "github_release",
    "tag": "v0.8.1",
    "assets": {
      "ue5.7": {
        "url": "https://github.com/loomle/loomle/releases/download/v0.8.1/loomle-bridge-0.8.1-ue5.7.zip",
        "sha256": "<64 lowercase hexadecimal characters>"
      },
      "ue5.8": {
        "url": "https://github.com/loomle/loomle/releases/download/v0.8.1/loomle-bridge-0.8.1-ue5.8.zip",
        "sha256": "<64 lowercase hexadecimal characters>"
      }
    }
  }
}
```

Fab omits `bridge` because the Fab package is the Bridge installation. Its
`listingUrl` is the Fab listing. No channel document may point to a GitHub
`latest/download` Bridge URL.

The document validator requires:

- an exact stable semantic version;
- a channel matching the file name;
- HTTPS URLs on approved Loomle, GitHub, Fab, Claude, or OpenAI origins;
- a release tag equal to `v${version}`;
- versioned Bridge asset names equal to the document version;
- SHA-256 values equal to the GitHub Release asset digests;
- no unknown fields.

## Status And Update Behavior

`status({})` remains read-only and performs no installation. Its Client block
adds the resolved distribution channel:

```text
client:
  version: 0.8.1
  distribution: claude
```

The update block retains `current`, `available`, and `unknown`:

```text
update:
  status: available
  version: 0.8.2
  authority: claude
  listing: "https://claude.ai/directory/connectors/loomle"
```

Channel-specific next actions are:

- GitHub: ask before replacing the complete GitHub UE plugin, preserving the
  current Windows process-stop and Editor-close guidance;
- Fab: open the Fab Library entry and let the Epic Games Launcher update the
  plugin; never download the GitHub package as a Fab Client update;
- MCP Registry: ask the current MCP host to upgrade the Registry package;
- Claude: update the Loomle extension through Claude Desktop;
- development: report update state as `unknown` and provide no replacement
  instruction.

An agent-channel Client separately reports the exact recommended GitHub Bridge
version and engine-specific URLs from its channel metadata. This is not called
a Client update. Installing a Bridge always requires user approval, a selected
UE version, closed affected Editors, checksum verification, and complete plugin
replacement.

Network timeout, malformed metadata, missing store APIs, Registry preview
changes, or rate limiting produce `update.status: unknown`. They never make
`status`, `sal_schema`, `agent_skill`, project binding, or UE-backed operations
fail.

## Publication And Promotion

### 1. GitHub release

The existing verified release workflow remains the artifact origin. A final
release publishes immutable versioned UE 5.7 and UE 5.8 packages and channel
candidates. The GitHub channel advances immediately.

### 2. Fab promotion

Fab promotion selects one existing GitHub tag, submits the exact Fab source
candidate for each supported engine version, and waits for approval. Only after
the new files are live does a maintainer advance `channels/fab.json`. The
manifest commit is the machine-readable record of the human Fab publication
gate.

### 3. MCP Registry promotion

The workflow selects one existing MCP Registry MCPB candidate, verifies its
hash, generates `server.json`, authenticates with GitHub OIDC, and runs:

```text
mcp-publisher publish
```

It then verifies the exact version through the Registry API. No existing
Registry version is edited or republished.

### 4. Claude promotion

The maintainer submits the exact Claude MCPB candidate and waits until the
directory version is available. Because Anthropic does not currently document
a stable public MCPB update API, this remains a human-controlled gate. Only
after publication does the maintainer advance `channels/claude.json`.

Promotion order is not coupled. A failed or delayed store promotion leaves that
channel pointer unchanged and does not block GitHub or another store.

## Implementation Delta

The repository now implements the Client channel resolver, native GitHub/Fab
identity, and shared agent candidate generation. The remaining items below are
the bounded path to live multi-channel publication; none requires separate
Client source trees.

### Client runtime — implemented

- Add a small channel resolver beside `client/src/status.ts`. It validates the
  packaging environment value or adjacent native metadata and otherwise
  selects `development`.
- Replace the single hard-coded GitHub lookup in `client/src/status.ts` with
  channel-specific read-only providers. Each provider returns the same internal
  update result and may fail independently to `unknown`.
- Extend the status schema and tests with `distribution`, `authority`, listing
  guidance, and the agent package's exact recommended Bridge release.
- Complete `client/src/tools.ts` metadata so every public tool has a title and
  explicit read-only, destructive, and open-world hints.

### Artifact assembly — implemented

- Extend `packaging/client/build.mjs` to accept and validate a distribution
  channel and to emit the adjacent metadata used by native packages.
- Extend `packaging/fab/assemble.mjs` so Fab always embeds `channel: "fab"`;
  the regular GitHub package embeds `channel: "github"`.
- Add one shared agent-package assembler that copies the verified
  `client/dist/main.cjs` into Registry MCPB, Claude MCPB, and Codex layouts,
  then proves all three copies have the same SHA-256.
- Generate distinct Registry and Claude MCPB manifests so their channel
  identity and promotion can remain independent without changing server code.
- Generate the Codex plugin and marketplace catalog from the same selected
  product tag, including `.codex-plugin/plugin.json` and `.mcp.json`.

### Channel metadata and publication — remaining

- Add strict source-controlled channel documents under `site/channels/` for
  Fab and Claude, plus a validator shared by local tests and CI.
- Extend the GitHub release workflow to publish immutable, versioned channel
  candidates alongside the verified UE packages. It must never promote a store
  merely because the candidate exists.
- Add a Registry promotion workflow that selects a candidate from an existing
  release, verifies its digest, publishes through `mcp-publisher`, and confirms
  the exact Registry API version. This is implemented.
- Keep Fab and Claude as explicit human approval gates. Their channel documents
  are advanced only after the maintainer verifies that the submitted version is
  public.

### Existing documentation

- Update `docs/CLIENT_STATUS_AND_UPDATES.md`, `client/README.md`, and the
  packaging READMEs only when the behavior is implemented; the present design
  must not be described there as current behavior.
- Update `site/install.md` with separate GitHub, Fab, Registry, and Claude
  installation paths. Codex uses the GitHub path. General website download links may continue to use
  GitHub `latest`, but generated agent metadata and Bridge recommendations must
  always use exact versioned URLs.
- Document the manual Claude MCPB replacement procedure only after it is
  confirmed in the live publisher interface.

## Integrity And Security

- Every public package is derived from an existing verified GitHub tag.
- Every package embeds the exact product and protocol versions from that tag.
- Native Clients retain the existing build receipts and platform verification.
- MCPB and Codex packages compare the SHA-256 of `loomle.cjs` against the
  verified Client bundle.
- Registry `server.json` records the exact MCPB SHA-256.
- Agent channel documents record exact GitHub Bridge SHA-256 values.
- Store updates never silently install or replace an Unreal plugin.
- Channel metadata cannot alter tool schemas, permissions, or executable
  arguments other than the fixed distribution identifier.
- `python` remains explicitly destructive/open-world and does not gain broader
  authority from store packaging.

## Required Verification

Implementation is not complete until tests prove:

- every public tool has a non-empty title and explicit `readOnlyHint`,
  `destructiveHint`, and `openWorldHint`;
- the channel resolver accepts only the five defined identifiers;
- Fab and GitHub native packages carry different channel metadata while their
  Client executable hashes remain identical;
- Registry MCPB, Claude MCPB, and Codex `loomle.cjs` hashes are identical;
- each package version, embedded Client version, Git tag, and Bridge asset
  version agree;
- an agent package never emits an unversioned GitHub Bridge URL;
- GitHub, Fab, Registry, and Claude update responses cannot cross
  authorities;
- malformed/offline update sources remain informational;
- the MCPB validates and installs on current Claude Desktop for macOS and
  Windows;
- the internal Codex compatibility package validates and identifies GitHub as
  its update authority;
- one real Node-bundle MCP session connects to the matching UE 5.7 and UE 5.8
  Bridge on both supported platforms;
- protocol mismatch is rejected before any UE tool dispatch.

## Documentation Sources

- [GitHub REST releases](https://docs.github.com/en/rest/releases/releases)
- [Publishing and updating Fab listings](https://dev.epicgames.com/documentation/fab/publishing-assets-for-sale-or-free-download-in-fab)
- [Obtaining Fab updates](https://dev.epicgames.com/documentation/fab/purchasing-and-downloading-assets-in-fab)
- [MCP Registry quickstart](https://modelcontextprotocol.io/registry/quickstart)
- [MCP Registry package types](https://modelcontextprotocol.io/registry/package-types)
- [MCP Registry versioning](https://modelcontextprotocol.io/registry/versioning)
- [MCP Registry API and aggregators](https://modelcontextprotocol.io/registry/registry-aggregators)
- [MCPB manifest specification](https://github.com/modelcontextprotocol/mcpb/blob/main/MANIFEST.md)
- [Building Claude MCPB extensions](https://claude.com/docs/connectors/building/mcpb)
- [Submitting Claude connectors](https://claude.com/docs/connectors/building/submission)
- [Managing Claude listings](https://claude.com/docs/connectors/building/after-publishing)
- [Building Codex plugins](https://developers.openai.com/codex/build-plugins)
- [Packaging OpenAI plugins](https://developers.openai.com/plugins/build/plugins)
- [Submitting OpenAI plugins](https://developers.openai.com/plugins/deploy/submission)

## Explicit Unknowns

The following behavior must be rechecked immediately before first publication:

- the Claude management path for replacing an already published MCPB;
- whether the Claude directory begins exposing an MCPB version or update API;
- whether OpenAI accepts bundled local MCP servers in the Universal Plugins
  Directory;
- MCP Registry preview schema or API changes.

Until an official interface exists, Loomle uses the conservative manual path
described above and never infers successful publication from an uploaded GitHub
artifact alone.
