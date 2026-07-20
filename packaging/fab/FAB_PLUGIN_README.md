# Loomle Bridge Fab Package

Loomle Bridge is an Unreal Editor code plugin that exposes the open Unreal
project to local AI coding agents through LOOMLE's MCP tools.

## Package Layout

- `Source/LoomleBridge`: Unreal Editor C++ bridge module.
- `Binaries/<platform>`: Bridge binary compiled by UE BuildPlugin for this
  package target.
- `Config/FilterPlugin.ini`: keeps non-UAsset runtime support files in the
  packaged plugin.
- `Resources/Loomle/<platform-arch>/loomle(.exe)`: self-contained Loomle Client
  used by Codex, Claude, and other MCP hosts through stdio.

## Unreal Plugin Dependencies

Loomle Bridge enables Unreal's `PCG` plugin for PCG graph tooling and
`PythonScriptPlugin` for the `execute` runtime bridge. Unreal Python belongs to
the in-editor fallback and is unrelated to the standalone Client. These are
Bridge-internal capabilities, not current public MCP tools.

## Loomle Client

The Fab package includes the matching platform Client under `Resources/Loomle`.
It already contains SAL, the Loomle interface catalog, the MCP implementation,
and its runtime dependencies. It does not require a separate Node.js, Python,
`uv`, or repository installation.

Typical command:

```bash
<PluginPath>/Resources/Loomle/<platform-arch>/loomle mcp
```

The Client discovers running Loomle Bridge instances through the local Loomle
runtime state and connects to the matching Unreal Editor project.

Documentation: https://loomle.ai/quickstart.html
Support: https://github.com/loomle/loomle/issues

When upgrading from Loomle 0.6, remove or move any old
`<Project>/Plugins/LoomleBridge` copy after backing up local modifications. A
project plugin with the same name takes precedence over this Fab engine plugin.
