# LOOMLE Docs

Current documentation is organized around the global install model.

## Core

- `LOOMLE_GLOBAL_INSTALL_MODEL.md`: global install, per-session attach, project
  discovery, update, and project plugin installation model.
- `REPO_STRUCTURE.md`: current source, release, global install, and target
  project layout.
- `MCP_PROTOCOL.md`: MCP tool surface and behavior contract.
- `ARCHITECTURE.md`: system boundary and runtime responsibility split.
- `LOOMLE_PERMISSION_MODEL.md`: trust and permission model.

## Packaging

- `../packaging/install/INSTALL_CONTRACT.md`: global bundle and installer
  contract.
- `../packaging/BOOTSTRAP_CONTRACT.md`: public install script contract.
- `../packaging/README.md`: maintainer-facing packaging commands.

## Graph Work

- `spec-graph-domain-split.md`: active spec for replacing the old unified
  `graph.*` tools with Blueprint, Material, and PCG namespaces.
- `issues/README.md`: local design issue index.

The legacy project-root client install docs were removed. Do not reintroduce
per-project client workspaces, project-root update scripts, or project-root MCP
client guidance in new docs.
