# LOOMLE 0.4.0 CLI Surface

This document defines the first `0.4.0` CLI cut.

## Decision

The project-local `loomle` binary should expose exactly one responsibility:

- act as the host-facing stdio MCP proxy for a single Unreal project

It should not remain a multi-command maintenance CLI.

## Public Binary Surface

Supported shape:

```text
Loomle/loomle [--project-root <ProjectRoot>]
```

Behavior:

- speak standard MCP over `stdin/stdout`
- connect directly to the project-local Unreal runtime endpoint
- forward `initialize`, `tools/list`, and `tools/call` to the native runtime

## Explicitly Removed From The Binary

These should not remain as `loomle` subcommands in the first `0.4.0` cut:

- `doctor`
- `session`
- `call`
- `list-tools`
- `server-path`
- `install`
- `update`
- `skill`

## Script Surface

Install and maintenance entrypoints move to scripts.

Bootstrap-only scripts:

- `install.sh`
- `install.ps1`

Installed project maintenance scripts:

- `Loomle/update.sh`
- `Loomle/update.ps1`
- `Loomle/doctor.sh`
- `Loomle/doctor.ps1`

## Connectivity Model

The binary should remain the single host-facing process:

```text
host <-> loomle(stdio MCP) <-> LoomleBridge(native MCP runtime)
```

It should not spawn a separate runtime server child process.

It should not require the host to connect directly to the Unreal runtime socket
or named pipe.
