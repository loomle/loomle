# LOOMLE Workspace Tools

This directory contains the project-local entry helpers and agent-facing guidance that ship with an installed LOOMLE workspace.

Included helpers:

- `doctor.sh` / `doctor.ps1`
  - verify that the installed project can find both the workspace and the plugin-side MCP server
- `run-server.sh` / `run-server.ps1`
  - launch the project-local MCP server through the canonical `Loomle/client/loomle` entrypoint
- `capabilities.md`
  - concise graph editing and layout expectations for Blueprint, Material, and PCG

These are workspace-facing materials, not repository-maintainer documentation.
