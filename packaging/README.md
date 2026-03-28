# Packaging Layer

This directory owns release assembly and installation rules.

Planned responsibilities:
- build release manifests
- assemble plugin, client, helper scripts, and workspace content into a release bundle
- install LOOMLE into a user project in one step

Canonical release helper paths:
- `packaging/bootstrap/install.sh`
- `packaging/bootstrap/install.ps1`
- `packaging/bootstrap/update.sh`
- `packaging/bootstrap/update.ps1`
- `packaging/bootstrap/BOOTSTRAP_CONTRACT.md`
- `packaging/bundle/build_release_manifest.py`
- `packaging/bundle/assemble_release_bundle.py`
- `packaging/manifests/`
- `packaging/install/`
- `packaging/install/install_release.py`
- `packaging/release/build_local_release.py`
- `packaging/install/install_from_checkout.py`

Current expectations:
- bootstrap downloads manifest + bundle directly, not an installer binary
- release bundles are complete when they include project-local `loomle`, install helper, maintenance scripts, plugin, and workspace content
- scripts should validate the declared bundle inputs before copying plugin and workspace content
- GitHub Releases is the current canonical host for published bundle and manifest assets; `loomle.ai` serves the bootstrap scripts and site content
