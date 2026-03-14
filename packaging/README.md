# Packaging Layer

This directory owns release assembly and installation rules.

Planned responsibilities:
- build release manifests
- assemble plugin, server, client, and workspace content into a release bundle
- install LOOMLE into a user project in one step

Canonical release helper paths:
- `packaging/bootstrap/install.sh`
- `packaging/bootstrap/install.ps1`
- `packaging/bootstrap/BOOTSTRAP_CONTRACT.md`
- `packaging/bundle/build_release_manifest.py`
- `packaging/bundle/assemble_release_bundle.py`
- `packaging/manifests/`
- `packaging/install/`
- `packaging/install/install_release.py`
- `packaging/release/build_local_release.py`
- `packaging/install/install_from_checkout.py`

Current expectations:
- bootstrap installs a machine-level `loomle` command
- release bundles are complete only when they include both the MCP server and the project-local LOOMLE client
- installers should validate the declared binaries before copying plugin and workspace content
