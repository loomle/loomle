# Packaging Layer

This directory owns release assembly and installation rules.

Planned responsibilities:
- build release manifests
- assemble plugin, server, client, and workspace content into a release bundle
- install LOOMLE into a user project in one step

Canonical release helper paths:
- `packaging/bundle/build_release_manifest.py`
- `packaging/manifests/`
- `packaging/install/`
- `packaging/install/install_release.py`
