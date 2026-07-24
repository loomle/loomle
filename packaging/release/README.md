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

The `assemble:fab` result is source staging, not a distributable or QA artifact.
`.github/workflows/verify-fab-mac.yml` is deliberately manual and read-only. It
builds the Client, assembles the source plugin, compiles an arm64-only plugin
with UE BuildPlugin, and uploads that verified BuildPlugin output as the QA
artifact. It does not create a tag, GitHub Release, or public Fab submission.

`.github/workflows/promote-github-release.yml` is a separate manual step. It
takes successful Mac and Windows verification run IDs, requires both runs to
belong to the same exact commit, checks out that commit, and verifies both sets
of result files, target descriptors, and archive hashes. It derives
`v<product-version>` and publishes both already-tested ZIPs and their SHA-256
sidecars without rebuilding or recompressing either candidate.

`.github/workflows/verify-fab-windows.yml` proves the native Windows x64 Client,
Bridge, Automation, package audit, and exact-ZIP end-to-end path. Windows is an
advertised prerelease target only when its successful run is paired with a
successful Mac run from the same commit during promotion.

Unsigned `0.7.0-rc.*` candidates may be published only as GitHub prereleases
whose checked-in notes explain the macOS Gatekeeper and Windows trust-warning
limitations. Stable promotion deliberately fails until the required platform
signing and trust gates are performed before packaged end-to-end verification.
Fab submission remains a separate human action.

Release notes are checked in under `packaging/release/notes/` and named by the
exact product version.

## Release Branches

- `0.6` is the maintenance line rooted at `v0.6.24`. It accepts only
  compatible fixes and produces any future `v0.6.x` releases.
- `main` is the `0.7` development line. Development builds use a prerelease
  product version; the release commit uses `0.7.0` and is tagged `v0.7.0`.

Product versions and RPC protocol compatibility remain independent. Change the
protocol version only when compatibility actually changes.
