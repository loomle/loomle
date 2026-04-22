# Packaging Layer

This directory owns release assembly and global installation rules.

Current responsibilities:
- build release manifests
- assemble the `loomle` binary and `LoomleBridge` plugin cache into a release bundle
- install LOOMLE once into the current user's global install root

Canonical release helper paths:
- `client/install.sh`
- `client/install.ps1`
- `packaging/BOOTSTRAP_CONTRACT.md`
- `packaging/bundle/build_release_manifest.py`
- `packaging/bundle/assemble_release_bundle.py`
- `packaging/manifests/`
- `packaging/release/build_local_release.py`

Current expectations:
- bootstrap downloads manifest + bundle directly, not an installer binary
- release bundles contain only:
  - `loomle(.exe)`
  - `plugin-cache/LoomleBridge/`
- release bundles do not include the old per-project workspace docs, guides, workflows, or examples
- install scripts are served by `loomle.ai`, not by GitHub release assets
- public install scripts materialize `~/.loomle` or `%USERPROFILE%\.loomle`
- UE project support is installed later through MCP `project.install`
- GitHub Releases is the canonical host for published bundle and manifest assets; `loomle.ai` serves the install scripts and site content
