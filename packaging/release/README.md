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
