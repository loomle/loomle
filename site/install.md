---
layout: default
title: Install
nav_order: 2
description: Track Loomle 0.7 Fab availability and connect its bundled Client when published.
---

# Install

Fab is the only planned installation channel for Loomle 0.7. The public
package is not available yet.

The current [Fab listing](https://www.fab.com/listings/f0fb545c-b1d9-4525-8642-3f170134c428)
still distributes Loomle 0.6 and does not match this 0.7 guide. The first
accepted 0.7 QA target is Unreal Engine 5.7 on Apple Silicon macOS. This page
will become the installation guide when the matching Fab build is published.

The installed plugin contains the Unreal Bridge source and the matching
self-contained Loomle Client. There is no website installer, global Loomle
installation, Python MCP server, `uv` environment, or project-local plugin
installation step.

## Upgrade From Loomle 0.6

Loomle 0.6 could install `LoomleBridge` inside each Unreal project. A project
plugin with the same name takes precedence over the Fab-installed engine
plugin, so leaving it in place silently keeps that project on 0.6.

Before installing or testing 0.7:

1. Close Unreal Editor for every affected project.
2. Check `<Project>/Plugins/LoomleBridge`.
3. If it is the old Loomle plugin, back up any local modifications, then remove
   it or move it outside the project's `Plugins` directory.
4. Install/enable the Fab plugin and restart the project.

Repeat this for every project that previously used the project-local plugin.
Fab cannot delete project files, and the 0.7 engine plugin cannot warn from a
project where the old copy shadows it.

## Install the Plugin

After Loomle 0.7 is published for the supported target:

1. Install the Loomle plugin for Unreal Engine 5.7 from Fab.
2. Enable `LoomleBridge` in Unreal Editor if it is not already enabled.
3. Restart Unreal Editor when prompted.
4. Open the Loomle toolbar/status panel.

Fab/Epic Launcher owns plugin installation and updates.

## Configure the MCP Host

The Loomle status panel locates the Client bundled for the current platform:

```text
<PluginPath>/Resources/Loomle/<platform-arch>/loomle(.exe)
```

The status panel detects Codex and Claude Desktop configuration, classifies an
existing Loomle entry, and provides exact copyable setup or migration guidance.
It never writes host configuration files. Recognized 0.6, stale, custom,
ambiguous, and unreadable states are reported without modification. Codex
detection respects `CODEX_HOME` and otherwise uses `~/.codex`.

Restart the MCP host after configuration so it launches the bundled Client.
The executable is self-contained; the machine does not need a separate Node.js,
Python, or `uv` installation.

## Manual Configuration

Use the absolute bundled Client path reported by the Loomle status panel.

Codex TOML:

```toml
[mcp_servers.loomle]
command = "/absolute/path/to/LoomleBridge/Resources/Loomle/<platform-arch>/loomle"
args = ["mcp"]
```

Claude Desktop-style JSON:

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

Use a real absolute path; do not copy the Client out of the plugin or replace
`<platform-arch>` literally. Windows is not yet an accepted 0.7 target and is
not implied by this configuration example.

## Runtime Discovery

When Unreal Editor runs with `LoomleBridge`, the plugin publishes a local
runtime record. The Client reads runtime records from:

```text
~/.loomle/state/runtimes
```

This directory is transient discovery state, not an installation. There is no
separate project installation, session attachment, global command, update
daemon, or project-local Client in 0.7. Fab updates the installed plugin and
its bundled Client together.

One MCP session binds to one project. Loomle auto-binds only when the project is
unambiguous; otherwise call `project({})` to inspect candidates, then bind one
returned `projectId`. The binding is sticky across Editor restarts. If that
project is offline, Loomle reports it and never silently switches to another
online project.

## Verify Setup

After restarting the MCP host:

1. Keep the target Unreal project open.
2. If Loomle asks for project selection, call `project({})` and bind the target
   project.
3. Call `editor_context`.
4. Confirm the result identifies the current editor surface or active asset.
5. Call `sal_schema` to see the active SAL module index.

Continue with the [Quickstart](quickstart.html).
