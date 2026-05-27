# Loomle Bridge Fab Package

Loomle Bridge is an Unreal Editor code plugin that exposes the open Unreal
project to local AI coding agents through LOOMLE's MCP tools.

## Package Layout

- `Source/LoomleBridge`: Unreal Editor C++ bridge module.
- `Config/FilterPlugin.ini`: keeps non-UAsset runtime support files in the
  packaged plugin.
- `Resources/MCP`: external Python MCP server source used by Codex, Claude,
  and other MCP hosts. This code is launched by the AI host through stdio; it
  is not an Unreal Python script executed inside the editor.

## Unreal Plugin Dependencies

Loomle Bridge enables Unreal's `PCG` plugin for PCG graph tooling and
`PythonScriptPlugin` for the `execute` runtime bridge. The Python MCP server in
`Resources/MCP` is separate from Unreal Python and is launched by the MCP host.

## Python MCP Server

The Fab package includes Python MCP source under `Resources/MCP` so the plugin
can configure a local MCP host without installing the native LOOMLE CLI.

Typical command:

```bash
uv --directory <PluginPath>/Resources/MCP run loomle_mcp_server.py
```

Requirements:

- Python 3.10 or newer.
- `uv` available on `PATH`.
- Internet access the first time `uv` resolves Python package dependencies,
  unless the user's environment already has the dependencies cached.

If a native LOOMLE installation is already configured, the Fab setup flow keeps
the native MCP configuration and uses this plugin as the Unreal bridge.

Documentation: https://loomle.ai/quickstart.html
Support: https://github.com/loomle/loomle/issues
