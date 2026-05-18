---
layout: default
title: Install
nav_order: 2
description: Install the global LOOMLE command and connect it to an MCP host.
---

# Install

LOOMLE is installed globally. Unreal projects receive only the
`Plugins/LoomleBridge` project plugin, installed or updated later through MCP
`project.install`.

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

## Project Setup

1. Open the target Unreal project.
2. If the project does not have LOOMLE support, call `project.install`.
3. Restart Unreal Editor when prompted.
4. Call `project.list`.
5. Call `project.attach` with the target online project.

`project.install` copies the cached global plugin into
`<ProjectRoot>/Plugins/LoomleBridge/`. Close the Unreal Editor for that project
before installing or updating project support.

## CLI Commands

- `loomle mcp`: run the stdio MCP server.
- `loomle update`: update the global LOOMLE install.
- `loomle doctor`: inspect the global install and print MCP configuration hints.

MCP tools are not duplicated as CLI subcommands.
