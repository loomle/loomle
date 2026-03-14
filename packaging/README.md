# Packaging Layer

This directory owns release assembly and installation rules.

Planned responsibilities:
- build release manifests
- assemble plugin, server, client, and workspace content into a release bundle
- install LOOMLE into a user project in one step

Migration note:
- Existing release helpers still live in `tools/`.
- Those scripts remain in use until packaging is migrated here.
