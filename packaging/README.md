# Packaging Layer

This directory owns release assembly and installation rules.

Planned responsibilities:
- build release manifests
- assemble plugin, client, maintenance scripts, and workspace content into a release bundle
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
- `packaging/release/build_local_release.py`
- `packaging/install/install_from_checkout.py`

Current expectations:
- bootstrap downloads manifest + bundle directly, not an installer binary
- release bundles contain only the installable project content:
  - `plugin/LoomleBridge/`
  - `Loomle/`
- install scripts are served by `loomle.ai`, not by GitHub release assets
- update scripts are installed into `Loomle/` and not published as standalone release assets
- scripts should validate the declared bundle inputs before copying plugin and workspace content, without invoking Python
- GitHub Releases is the canonical host for published bundle and manifest assets; `loomle.ai` serves the install scripts and site content
