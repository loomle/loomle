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
the manual Mac verification workflow:

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
takes a successful Mac verification run ID, checks out that run's exact commit,
verifies its result files and archive hash, derives `v<product-version>`, and
publishes the already-tested ZIP without rebuilding or recompressing it.

`.github/workflows/verify-fab-windows.yml` independently proves the native
Windows x64 Client, Bridge, Automation, package audit, and exact-ZIP end-to-end
path. Its uploaded ZIP is QA-only. The current promotion workflow does not
consume it, and a successful run does not change advertised release platforms.

Unsigned `0.7.0-rc.*` candidates may be published only as GitHub prereleases
whose checked-in notes explain the macOS Gatekeeper limitation. Stable
promotion deliberately fails until Developer ID signing and notarization are
performed before packaged end-to-end verification. Fab submission remains a
separate human action.

Release notes are checked in under `packaging/release/notes/` and named by the
exact product version.

## Release Branches

- `0.6` is the maintenance line rooted at `v0.6.24`. It accepts only
  compatible fixes and produces any future `v0.6.x` releases.
- `main` is the `0.7` development line. Development builds use a prerelease
  product version; the release commit uses `0.7.0` and is tagged `v0.7.0`.

Product versions and RPC protocol compatibility remain independent. Change the
protocol version only when compatibility actually changes.
