---
layout: default
title: Install
nav_order: 2
description: Install the global LOOMLE command and connect it to an MCP host.
---

# Install

LOOMLE has a global install and per-project support.

The installer sets up the global `loomle` command. It does not automatically
modify every Unreal project on the machine. A project receives LOOMLE support
only when `project.install` is run for that project.

## macOS and Linux

```bash
curl -fsSL https://loomle.ai/install.sh | bash
```

## Windows PowerShell

```powershell
& ([scriptblock]::Create((irm https://loomle.ai/install.ps1)))
```

## MCP Host Command

Configure Codex, Claude, or another MCP host to run:

```bash
loomle mcp
```

LOOMLE does not use a daemon. Each MCP host session starts its own stdio
`loomle mcp` process and attaches to one active Unreal project in that process.

## MCP Host Setup

The installer attempts common MCP host setup automatically:

- Codex: if `~/.codex` exists, the installer writes a `loomle` server entry to
  `~/.codex/config.toml`.
- Claude: if the `claude` CLI is available, the installer runs
  `claude mcp add --scope user loomle -- <loomle> mcp`.

If automatic setup is skipped or you use a different install root, configure the
host manually with the stable global command path:

```bash
codex mcp add loomle -- ~/.loomle/bin/loomle mcp
claude mcp add --scope user loomle -- ~/.loomle/bin/loomle mcp
```

For other MCP hosts, add a stdio server named `loomle` with:

```json
{
  "command": "~/.loomle/bin/loomle",
  "args": ["mcp"]
}
```

After changing MCP configuration, restart the MCP host session. Use
`loomle doctor` to inspect the global install and print host configuration
hints.

## After Install

The installer creates a global install under `~/.loomle` unless
`LOOMLE_INSTALL_ROOT` is set:

- `~/.loomle/bin/loomle`: stable command used by shells and MCP hosts.
- `~/.loomle/install/active.json`: active LOOMLE version and binary path.
- `~/.loomle/versions/<version>/`: installed release payloads.
- `~/.loomle/versions/<version>/plugin-cache/LoomleBridge/`: plugin source used
  for project installs.
- `~/.loomle/state/`: known project and runtime records.

Updating LOOMLE updates the global command and active version. Registered
offline projects may also be synced to the active plugin version. Projects that
are open in Unreal Editor are skipped and should be updated after the editor is
closed.

## Project Setup

1. Close Unreal Editor for the target project.
2. Call `project.install` with the target `projectRoot`.
3. Open or restart Unreal Editor for that project.
4. Call `project.list`.
5. Call `project.attach` with the target online project.

`project.install` copies the cached global plugin into
`<ProjectRoot>/Plugins/LoomleBridge/`, records the project under the global
LOOMLE state directory, and applies LOOMLE's required editor support setting.
It refuses to run while that project is online in Unreal Editor.

When Unreal Editor starts with `LoomleBridge` loaded, the plugin reports a
runtime endpoint for that project. `project.list` discovers those online
projects, and `project.attach` selects one for the current MCP session. Attach
state is per MCP session; there is no global active Unreal project.

## CLI Commands

- `loomle mcp`: run the stdio MCP server.
- `loomle update`: update the global LOOMLE install.
- `loomle doctor`: inspect the global install and print MCP configuration hints.

MCP tools are not duplicated as CLI subcommands.
