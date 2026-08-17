# Loomle Release Work

The root `package.json` is the only product-version source on `main`. Change it
without creating a tag, then regenerate and check its derived values:

```sh
npm version <version> --no-git-tag-version
npm run generate:version
npm test
```

`npm run generate:version` updates the generated Client product-version module,
`LoomleBridge.uplugin` `VersionName`, and the generated Client and Bridge
protocol-version constants. It does not change the independent Fab build
number in `LoomleBridge.uplugin` `Version`.

Prepare the pre-BuildPlugin staging tree with the same initial stages used by
the native verification workflows:

```sh
npm ci
npm test
npm run build:executable -- --target darwin-arm64
npm run test:executable -- --target darwin-arm64
npm run assemble:fab -- \
  --output-dir .tmp/fab/darwin-arm64 \
  --target darwin-arm64
```

The `assemble:fab` result is the canonical package tree before UE compilation.
`.github/workflows/verify-fab-mac.yml` is deliberately manual and read-only. It
builds the Client, assembles the source plugin, archives that exact tree as the
Fab source candidate, temporarily overlays the independent Automation module,
and compiles an arm64-only plugin with one UE BuildPlugin invocation. Automation
runs against that output. Finalization then removes only the test module and
proves the production binary hash is unchanged before uploading the complete
GitHub QA candidate. It verifies that the latter retains every Fab source file
unchanged and only adds UE `Binaries/` plus BuildPlugin descriptor installation
fields. It does not create a tag, GitHub Release, or public Fab submission.

`.github/workflows/promote-github-release.yml` is a separate manual step. It
takes successful Mac and Windows verification run IDs, requires both runs to
belong to the same exact commit, checks out that commit, and verifies both sets
of result files, target descriptors, and archive hashes. It then rejects shared
source drift, except for a strictly verified historical CRLF/LF-only text
difference that is emitted as LF, and mechanically merges the fragments into one
cross-platform Fab and GitHub source archives plus separate UE 5.7 and UE 5.8
complete GitHub plugin archives. The archives share exact Client executable
bytes; only their adjacent distribution metadata differs.
No executable bytes are rebuilt or rewritten. It derives `v<product-version>`,
requires an existing lightweight tag at the exact verified commit, and
publishes those four merged ZIPs and their SHA-256 sidecars. Final releases
also publish byte-identical stable aliases (`loomle-bridge-ue5.7.zip`,
`loomle-bridge-ue5.8.zip`, and `loomle-bridge-source.zip`, with matching
sidecars) so the website can use one
`releases/latest/download/...` URL across versions. Prereleases do not publish
stable aliases. The Client reads GitHub's public latest-release API directly
and binds `tag_name`, the versioned plugin asset URL, and that asset's GitHub
SHA-256 digest from one Release response. A successful final promotion is
therefore the complete update-publication step for Clients beginning with
0.7.7. As a one-time migration exception, after 0.7.7 promotion a maintainer
updates the legacy website manifest from 0.7.6 to 0.7.7 with the published
stable-alias SHA-256, then freezes that file permanently. This lets older
Clients discover the first GitHub-native updater without making the website a
continuing release source. A trusted maintainer pushes the exact tag before
promotion because the Actions
installation token cannot create a tag that exposes historical workflow-file
changes and cannot reliably pass an older commit through
`gh release create --target`.

`.github/workflows/verify-fab-windows.yml` proves the native Windows x64 Client,
Bridge, Automation, Fab-source archive, package-derivation contract, and
exact-ZIP end-to-end path. Windows is an advertised prerelease target only when
its successful run is paired with a successful Mac run from the same commit
during promotion; the same paired-run rule applies to final releases.

Each self-hosted native runner must have official UE 5.7 and UE 5.8 installs.
The matrix runs them serially to avoid overlapping Editor or build processes.
Mac defaults to Epic Launcher roots under `/Users/Shared/Epic Games` and may
override them with `UE_5_7_ROOT_MAC` and `UE_5_8_ROOT_MAC` repository variables.
Windows uses `UE_5_7_ROOT_WINDOWS` and `UE_5_8_ROOT_WINDOWS`, with its existing
`D:\Dev` discovery as a fallback. Every resolved root is checked against
`Engine/Build/Build.version` before compilation.

Unsigned candidates may be published when their checked-in notes explain the
macOS Gatekeeper and Windows trust-warning limitations. Signing is a release
quality improvement, not a promotion prerequisite for the agent-invoked
standalone Client. Every published binary must still pass the exact packaged
end-to-end and checksum gates. Fab submission remains a separate human action.

The same promotion builds the platform-neutral Client once from the exact
verified commit and publishes immutable agent-channel candidates:

- `loomle-mcp-registry-<version>.mcpb`, its checksum, and `server.json`;
- `loomle-claude-<version>.mcpb` and its checksum.

Their embedded `loomle.cjs` hashes must match one another and the internally
validated Codex compatibility copy. Publishing these files to the GitHub
Release does not publish or advance MCP Registry or Claude. Each channel keeps
its own later promotion or review gate, and no agent candidate receives an
unversioned stable alias. Codex users follow the GitHub distribution; no Codex
marketplace ZIP is published.

`.github/workflows/promote-mcp-registry.yml` is a separate manual promotion
step. It accepts an existing final GitHub Release tag, downloads the exact
Registry MCPB, checksum, and `server.json`, verifies their version, manifest,
URL, and SHA-256 binding, rejects an already published immutable version,
authenticates with GitHub OIDC, and publishes with a checksum-pinned
`mcp-publisher`. It then verifies the exact Registry API response.

For each accepted target, the verification workflow uploads internal QA
fragments:

- one `loomle-fab-source-ue5.7-<platform-arch>.zip` and checksum per platform;
- `loomle-fab-plugin-ue<engine-version>-<platform-arch>.zip` and checksum.

These are not public or separately maintained packages. The second is compiled
from the first, and `packaging/fab/verify-derivation.mjs` rejects source drift,
omitted source, or non-build additions. Promotion merges the two native pairs
and publishes:

- `loomle-bridge-<version>-fab-source.zip` for the single Fab Project File Link;
- `loomle-bridge-<version>-source.zip` for GitHub source installation;
- `loomle-bridge-<version>-ue5.7.zip` for UE 5.7 installations;
- `loomle-bridge-<version>-ue5.8.zip` for UE 5.8 installations.

Fab always receives the immutable versioned Fab-source URL. GitHub stable
aliases remain byte-identical to the versioned GitHub artifacts. The Fab and
GitHub source archives are mechanically derived from the same verified inputs;
their Client executables are identical and only `distribution.json` differs.

Release notes are checked in under `packaging/release/notes/` and named by the
exact product version.

## Release Branches

- `0.6` is the maintenance line rooted at `v0.6.24`. It accepts only
  compatible fixes and produces any future `v0.6.x` releases.
- `main` is the `0.7` development line. Development builds use a prerelease
  product version; the current release commit uses `0.7.13` and is tagged
  `v0.7.13`.

Product versions and RPC protocol compatibility remain independent. Change the
protocol version only when compatibility actually changes.
