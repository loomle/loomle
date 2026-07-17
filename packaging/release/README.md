# LOOMLE Release Scripts

This directory contains source-checkout release helpers.

Primary entrypoints:

- `../tools/bump_release_version.py`
  - update `client/Cargo.toml`
  - update `client/Cargo.lock`
  - update `LoomleBridge.uplugin` `VersionName`
  - increment `LoomleBridge.uplugin` `Version`
- `build_local_release.py`
  - build the Rust server and client
  - assemble a release bundle
  - generate a local manifest
  - emit a zip archive for the assembled bundle

These scripts are for maintainers and local source-checkout workflows, not for the installed project workspace.

## Release Branches

- `0.6` is the maintenance line rooted at `v0.6.24`. It accepts only
  compatible fixes and produces any future `v0.6.x` releases.
- `main` is the `0.7` development line. Development builds use a prerelease
  product version; the release commit uses `0.7.0` and is tagged `v0.7.0`.

Product versions and RPC protocol compatibility remain independent. Change the
protocol version only when compatibility actually changes.
