# Workspace Layer

This directory contains the source template for the installed user-project `Loomle/` directory.

Everything under `workspace/Loomle/` is intended to be copied into a user's project during installation.

Planned contents:
- one root README that acts as the Agent-facing starting point
- one project-local client entrypoint under `Loomle/client/`
- workflow-specific operating guides under `Loomle/workflows/`
- small payload examples under `Loomle/examples/`
- machine-written runtime state under `Loomle/runtime/`

Design rule:
- keep the installed workspace smaller and simpler than the repository layout
- do not mirror repository maintainer docs or internal helper scripts into the installed project unless the Agent needs them directly
