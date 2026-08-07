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
Fab source candidate, compiles an arm64-only plugin with UE BuildPlugin, and
uploads the complete BuildPlugin output as the GitHub QA candidate. It verifies
that the latter retains every Fab source file unchanged and only adds UE
`Binaries/` plus BuildPlugin descriptor installation fields. It does not create
a tag, GitHub Release, or public Fab submission.

`.github/workflows/promote-github-release.yml` is a separate manual step. It
takes successful Mac and Windows verification run IDs, requires both runs to
belong to the same exact commit, checks out that commit, and verifies both sets
of result files, target descriptors, and archive hashes. It then rejects shared
source drift, except for a strictly verified historical CRLF/LF-only text
difference that is emitted as LF, and mechanically merges the fragments into one
cross-platform source archive and one cross-platform complete plugin archive.
No executable bytes are rebuilt or rewritten. It derives `v<product-version>`,
requires an existing lightweight tag at the exact verified commit, and
publishes those two merged ZIPs and their SHA-256 sidecars. Final releases also
publish byte-identical stable aliases (`loomle-bridge.zip` and
`loomle-bridge-source.zip`, with matching sidecars) so the website can use one
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

Unsigned candidates may be published when their checked-in notes explain the
macOS Gatekeeper and Windows trust-warning limitations. Signing is a release
quality improvement, not a promotion prerequisite for the agent-invoked
standalone Client. Every published binary must still pass the exact packaged
end-to-end and checksum gates. Fab submission remains a separate human action.

For each accepted target, the verification workflow uploads internal QA
fragments:

- `loomle-fab-source-<platform-arch>.zip` and checksum;
- `loomle-fab-plugin-<platform-arch>.zip` and checksum.

These are not public or separately maintained packages. The second is compiled
from the first, and `packaging/fab/verify-derivation.mjs` rejects source drift,
omitted source, or non-build additions. Promotion merges the two native pairs
and publishes:

- `loomle-bridge-<version>-source.zip` for the single Fab Project File Link;
- `loomle-bridge-<version>.zip` as the single GitHub installation package.

Fab always receives the immutable versioned source URL. The stable aliases are
GitHub download names for the same bytes, not separately built packages.

Release notes are checked in under `packaging/release/notes/` and named by the
exact product version.

## Release Branches

- `0.6` is the maintenance line rooted at `v0.6.24`. It accepts only
  compatible fixes and produces any future `v0.6.x` releases.
- `main` is the `0.7` development line. Development builds use a prerelease
  product version; the current release commit uses `0.7.7` and is tagged
  `v0.7.7`.

Product versions and RPC protocol compatibility remain independent. Change the
protocol version only when compatibility actually changes.
