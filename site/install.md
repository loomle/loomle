---
layout: default
title: Install
nav_order: 2
description: Install LOOMLE from the native installer or from Fab.
---

# Install

LOOMLE can be installed through two channels:

- Native install from this website.
- Fab install from Epic/Fab.

Both paths connect Claude Code, Codex, or another MCP-compatible host to the
same Unreal Editor bridge and tool surface.

## Choose an Install Path

### Option A: Native Install

Use the native install if you want the full CLI workflow, direct LOOMLE updates,
and explicit project plugin management.

Native install:

- Installs a global `loomle` command under `~/.loomle` by default.
- Provides `loomle mcp` for MCP hosts.
- Can install or update project-local `Plugins/LoomleBridge` with
  `project.install`.
- Updates through `loomle update` or the website installer.

### Option B: Fab Install

Use the Fab install if you want to install LOOMLE from Epic/Fab and manage the
Unreal plugin through Epic Launcher.

[Install LOOMLE MCP for Unreal on Fab](https://www.fab.com/listings/3865e6f0-77b9-47bf-bd55-9c7b0b4768e7){: .btn .btn-primary }

Fab install:

- Installs `LoomleBridge` through Fab/Epic Launcher.
- Includes the Python MCP server source used by the Fab setup flow.
- Uses the Loomle toolbar inside Unreal Editor to check setup status.
- Lets Fab/Epic Launcher own plugin updates.
- Keeps an existing native `loomle mcp` setup if one is already configured.

## Native Install

## macOS and Linux

```bash
curl -fsSL https://loomle.ai/install.sh | bash
```

## Windows PowerShell

```powershell
& ([scriptblock]::Create((irm https://loomle.ai/install.ps1)))
```

### MCP Host Command

Configure Codex, Claude, or another MCP host to run:

```bash
loomle mcp
```

LOOMLE does not use a daemon. Each MCP host session starts its own stdio
`loomle mcp` process and attaches to one active Unreal project in that process.

### MCP Host Setup

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

### After Native Install

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

### Native Project Setup

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

## Fab Install

1. Install
   [LOOMLE MCP for Unreal](https://www.fab.com/listings/3865e6f0-77b9-47bf-bd55-9c7b0b4768e7)
   from Fab.
2. Enable the `LoomleBridge` plugin in Unreal Editor if needed.
3. Restart Unreal Editor.
4. Open the Loomle toolbar or status panel.
5. If Codex or Claude configuration is detected and no existing LOOMLE MCP
   server is configured, LOOMLE can configure the Fab Python MCP server.
6. If a native `loomle mcp` setup already exists, keep using it. The native MCP
   server can connect to the Fab-installed plugin.
7. Restart the AI host, then call `project.list` and `project.attach`.

Fab install does not create a global `~/.loomle/bin/loomle` command. Plugin
updates are owned by Fab/Epic Launcher. If you later install native LOOMLE, the
native MCP server should use the existing Fab-managed plugin instead of
overwriting it.

### Fab Python MCP Location

The Python MCP server used by the Fab setup flow is inside the installed Unreal
plugin:

```text
<PluginPath>/Resources/MCP/
  pyproject.toml
  loomle_mcp_server.py
  loomle_mcp/
  tool-manifest/
```

`<PluginPath>` is the actual `LoomleBridge` plugin folder reported by Unreal.
Depending on how Fab/Epic Launcher installed the plugin, that can be an Engine
plugin path or a project plugin path such as:

```text
<ProjectRoot>/Plugins/LoomleBridge
```

In the LOOMLE source repository, the same Python MCP server is maintained under
`mcp/python/`. The Fab package copies that source into
`LoomleBridge/Resources/MCP/`; users should configure MCP hosts against the
installed plugin's `Resources/MCP` directory, not the source checkout.

### What Fab Automatic Setup Writes

When the Loomle setup panel can safely configure an MCP host, it does not
install a daemon and it does not replace unrelated MCP servers. It:

- Detects the installed `LoomleBridge` plugin path.
- Checks that `<PluginPath>/Resources/MCP/loomle_mcp_server.py` exists.
- Builds a stdio MCP server entry named `loomle`.
- Backs up the host config before editing it.
- Merges only the `loomle` entry.
- Keeps an existing native `loomle mcp` entry unchanged.

For Codex, the Fab Python MCP entry in `~/.codex/config.toml` is:

```toml
[mcp_servers.loomle]
command = "uv"
args = ["--directory", "<PluginPath>/Resources/MCP", "run", "loomle_mcp_server.py"]
```

For Claude Desktop-style JSON config, the equivalent entry is:

```json
{
  "mcpServers": {
    "loomle": {
      "command": "uv",
      "args": [
        "--directory",
        "<PluginPath>/Resources/MCP",
        "run",
        "loomle_mcp_server.py"
      ]
    }
  }
}
```

After editing MCP configuration, restart the AI host so it starts the new stdio
server.

### Manual Fab MCP Configuration

If automatic setup is not available, add a stdio MCP server named `loomle` with
the same command:

```json
{
  "command": "uv",
  "args": [
    "--directory",
    "<PluginPath>/Resources/MCP",
    "run",
    "loomle_mcp_server.py"
  ]
}
```

This runs the Python MCP server from the Fab-installed plugin resources. The
server discovers online Unreal projects through LOOMLE runtime records, so once
Unreal Editor is open with `LoomleBridge` loaded, use `project.list` and
`project.attach` from the MCP host.

## How the Paths Work Together

- Native CLI can connect to a Fab-installed `LoomleBridge` plugin.
- Fab setup does not replace an existing native `loomle mcp` configuration.
- Native `project.install` should not overwrite a Fab-managed plugin.
- Native updates are owned by `loomle update` or the website installer.
- Fab plugin updates are owned by Fab/Epic Launcher.

## CLI Commands

- `loomle mcp`: run the stdio MCP server.
- `loomle update`: update the global LOOMLE install.
- `loomle doctor`: inspect the global install and print MCP configuration hints.

MCP tools are not duplicated as CLI subcommands.
