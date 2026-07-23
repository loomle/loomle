---
layout: default
title: Install
nav_order: 2
description: Install Loomle 0.7 from Fab, configure an MCP host, and verify the connection.
---

# Install

Loomle 0.7 uses Fab as its only installation channel. The plugin contains both
the Unreal Bridge and the matching self-contained Loomle Client.

{: .warning }
> Loomle 0.7 is not public yet. The current
> [Fab listing](https://www.fab.com/listings/f0fb545c-b1d9-4525-8642-3f170134c428)
> still distributes Loomle 0.6 and does not match this guide. Do not combine
> the 0.6 package with the 0.7 documentation.

## Compatibility

The first accepted Loomle 0.7 QA target is:

| Component | Accepted target |
| --- | --- |
| Unreal Engine | 5.7 |
| Operating system | macOS |
| Architecture | Apple Silicon |
| Installation channel | Fab |

Other engine versions and platforms are not implied until they pass the same
native and packaged verification.

The 0.7 plugin contains the Client at:

```text
<PluginPath>/Resources/Loomle/<platform-arch>/loomle(.exe)
```

The executable is self-contained. The machine does not need a separate
Python, `uv`, Node.js, or global Loomle installation.

## Upgrade From Loomle 0.6

Loomle 0.6 could install `LoomleBridge` inside each Unreal project. A
project-local plugin with the same name takes precedence over the
Fab-installed engine plugin, so leaving an old copy in place can silently keep
that project on 0.6.

Before installing or testing 0.7:

1. Close Unreal Editor for every affected project.
2. Check `<Project>/Plugins/LoomleBridge`.
3. If it is the old Loomle plugin, back up local modifications.
4. Remove it or move it outside the project's `Plugins` directory.
5. Repeat the check for every project that previously used Loomle 0.6.

Fab cannot remove project files, and a shadowed 0.7 engine plugin cannot warn
from a project that loaded the old copy.

## Install the Plugin

After the matching Loomle 0.7 package is published:

1. Install Loomle for Unreal Engine 5.7 from Fab.
2. Open the target Unreal project.
3. Enable `LoomleBridge` if it is not already enabled.
4. Restart Unreal Editor when prompted.
5. Open the Loomle toolbar/status panel.

Fab and Epic Launcher own plugin installation and updates.

## Configure the MCP Host

The Loomle status panel reports the exact bundled Client path for the current
platform and gives copyable host configuration guidance. It detects supported
Codex and Claude Desktop configurations and classifies exact, missing, 0.6,
stale, custom, ambiguous, and unreadable entries.

The panel never modifies host configuration files. Copy the guidance, update
the host configuration yourself, then restart the MCP host.

### Codex

Use the absolute path reported by the Loomle panel:

```toml
[mcp_servers.loomle]
command = "/absolute/path/to/LoomleBridge/Resources/Loomle/<platform-arch>/loomle"
args = ["mcp"]
```

### Claude Desktop-style hosts

```json
{
  "mcpServers": {
    "loomle": {
      "command": "/absolute/path/to/LoomleBridge/Resources/Loomle/<platform-arch>/loomle",
      "args": ["mcp"]
    }
  }
}
```

Replace the example with the real absolute path. Do not copy the Client out of
the plugin and do not use `<platform-arch>` literally.

## Verify the Connection

After restarting the MCP host:

1. Keep the target Unreal project open.
2. Call `project({})`.
3. If the session is not bound, copy the returned `projectId` into
   `project({ projectId: "<id>" })`.
4. Call `editor_context({})`.
5. Confirm that the result identifies the current editor surface or active
   asset.
6. Call `sal_schema({})` and confirm that the active interface index is
   available.

`sal_schema` is local and works without an online Editor. The other UE-backed
tools require the session's bound project to have one healthy Editor runtime.

Continue with the [Quickstart](quickstart.html).

## Project Discovery

When Unreal Editor runs with `LoomleBridge`, the plugin publishes transient
local discovery state under:

```text
~/.loomle/state/runtimes
```

This directory is not an installation. One MCP session binds to one project,
and that binding remains sticky across Editor restarts. If the bound project
goes offline, Loomle reports it instead of silently switching to another
online project.

See [Project Binding](calls/project.html) for the complete user-facing model.
