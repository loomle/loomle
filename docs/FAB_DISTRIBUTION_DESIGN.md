# Fab Distribution Design

## Intent

LOOMLE should be publishable on Fab as a complete Unreal-facing product while
keeping the existing global installer path.

The Fab package should make first install easier for Unreal users:

1. Install the LOOMLE UE plugin from Fab.
2. Enable the plugin in an Unreal project.
3. Use an in-editor setup panel to configure the bundled Python MCP server, or
   keep an existing native `loomle mcp` setup.
4. Configure Claude Code, Codex, or another MCP host to run LOOMLE MCP.
5. Attach the running Unreal project.

The goal is one product with two installation entrypoints:

- Website/GitHub release installer: installs the global CLI first, then
  installs project support with `project.install`.
- Fab installer: installs the UE plugin first, then exposes a bundled Python
  MCP server from inside the plugin package.

## External Requirements And Assumptions

Fab accepts Unreal Engine Tools & Plugins as Code Plugins. The Fab technical
requirements are the governing source and may change over time:

- <https://www.fab.com/o/technical-requirements>

Unreal plugins are discovered from Engine or Project plugin locations and are
enabled per project:

- <https://dev.epicgames.com/documentation/en-us/unreal-engine/working-with-plugins-in-unreal-engine>

Public competitor pages show that Fab MCP products commonly ship an Unreal
plugin plus an external MCP runtime requirement. `ClaudeUnreal`, for example,
uses a Fab plugin and runs a Python MCP server with `uv` from plugin resources:

- <https://echoulen.github.io/claude-unreal/>

The safest LOOMLE assumption is:

- The Fab submission must remain a valid UE Code Plugin.
- `Source/` must remain the UE plugin source-of-truth.
- UE-generated `Intermediate` and `Saved` directories should not be submitted.
- The Fab Python MCP server is a lightweight MCP runtime that reads the same
  tool manifest and talks to the same Bridge RPC as the native CLI.
- The native `loomle` binary remains the website/GitHub release entrypoint and
  may still connect to Fab-installed plugins through the shared runtime
  registry.

## Package Shape

Fab should receive a package rooted at the UE plugin folder.

The first Fab submission shape avoids bundling native executable payloads. It
ships the UE plugin plus a Python MCP runtime under plugin resources:

```text
LoomleBridge/
  LoomleBridge.uplugin
  Config/
  Source/
    LoomleBridge/
      LoomleBridge.Build.cs
      Public/
      Private/
  Resources/
    Icon128.png
    MCP/
      pyproject.toml
      loomle_mcp_server.py
      loomle_mcp/
      tool-manifest/
    install.md
```

The Fab package is still one UE plugin. The Python MCP runtime is not a second
top-level product folder and does not replace the native CLI release.

`Config/FilterPlugin.ini` must preserve `Resources/MCP` so Fab's BuildPlugin
step does not discard the Python runtime or the shared tool manifest.

The existing GitHub release bundle remains:

```text
loomle(.exe)
plugin-cache/
  LoomleBridge/
```

Both package shapes should be built from the same release inputs:

- `engine/LoomleBridge`
- `mcp/python`
- release version metadata

## Runtime Install Model

The Fab-installed plugin must not depend on being copied into
`<ProjectRoot>/Plugins/LoomleBridge`.

When the plugin starts, it already writes runtime state under the global LOOMLE
home:

```text
~/.loomle/state/runtimes/<projectId>.json
~/.loomle/state/projects/<projectId>.json
```

This means Fab Engine Plugin installs and project-local installs can share the
same attach model.

Runtime records must describe the real plugin installation path and ownership.
Fab/Engine plugin installs cannot be represented as
`<ProjectRoot>/Plugins/LoomleBridge`.

Required fields:

- `pluginPath`: actual `IPluginManager::FindPlugin("LoomleBridge")->GetBaseDir()`.
- `pluginInstallScope`: `project` or `engine`.
- `pluginManagedBy`: `native`, `fab`, `external`, or `unknown`.

The setup panel should support these states:

- `Bridge Ready`: plugin loaded and local RPC endpoint available.
- `Native CLI Configured`: Codex or Claude already uses native `loomle mcp`.
- `Fab Python MCP Available`: `Resources/MCP/loomle_mcp_server.py` and manifest
  are present in the plugin package.
- `MCP Host Configured`: Codex or Claude user config contains the LOOMLE MCP
  entry.
- `Project Attached`: current MCP session has attached to this runtime.

## Unreal Setup Panel

The existing LOOMLE toolbar status badge should become the entrypoint for Fab
setup without turning into a large in-editor app.

UI shape:

- Keep the compact toolbar badge: `Loomle Ready`, `Loomle Starting`,
  `Loomle PIE`, `Loomle Degraded`, or `Loomle Offline`.
- Clicking the badge opens a small popup menu/panel.
- The panel shows a short human-facing summary first: one line for Bridge state
  and one line for MCP setup/client state. Endpoint, plugin path, config paths,
  and last activity stay under `Advanced details`.
- The panel must not require an MCP host to already be connected.
- The panel should not run arbitrary shell commands or edit config on editor
  startup. Safe config writes may happen only when the user opens the setup
  panel and the host/config path are detected clearly.

Advanced details:

- `Bridge`: state, endpoint, project id.
- `Plugin`: version, install scope, managed by, plugin path.
- `MCP`: native configured, Fab Python MCP available, Codex/Claude status.
- `Client`: recent MCP activity observed by the Bridge.
- `Last activity`: last observed RPC method/tool, if any.

First-version actions:

- If Codex or Claude is detected and no `loomle` entry exists, the panel safely
  backs up and merges the recommended `loomle` MCP entry automatically.
- If an existing `loomle` entry is found, the panel keeps it unchanged and
  tells the user to continue with that setup.
- The panel exposes at most one action: `Copy Setup Prompt`. It appears only
  when no Codex/Claude MCP setup or host config is detected, and exists solely
  for first-time initialization. Once any host config or `loomle` entry is
  detected, the panel becomes status-only instead of prompting the user to copy
  text.
- If a client is currently connected, the main text should say so. Recent
  activity without an active connection stays in advanced details so the main
  text does not confuse activity with connectivity.

Deferred actions:

- Manual `Connect Codex` or `Connect Claude` buttons are intentionally omitted;
  detectable hosts are configured automatically, and undetected hosts are guided
  through docs plus a prompt.
- Downloading native payloads from inside UE is deferred.
- Switching ownership between Fab-managed and native-managed plugins is
  deferred.

Implementation boundary:

- The UE panel builds its own local snapshot from Bridge runtime state, plugin
  path, `Resources/MCP`, and known config paths.
- Config writes must use the same safety policy as `setup.configure`: no
  overwrite, backup before write, preserve unrelated settings, and keep native
  entries.
- Client activity is runtime-observed, not inferred from config files. The
  Bridge records the last valid RPC method/tool it received and treats recent
  activity as evidence that an MCP client is actually connected to this project.
- Native/Python MCP `setup.status` remains the canonical agent-facing schema.
- Field names and enum wording should stay aligned with `setup.status` even if
  the UE panel only renders a summarized view.

## Fab Setup Panel

The Fab setup panel should not silently replace an existing native LOOMLE MCP
configuration.

The setup panel should be driven by a read-only status model first. The status
model may be exposed through an internal bridge call and later through a public
tool named `setup.status`, but it must not modify user config.

Proposed `setup.status` response:

```json
{
  "schemaVersion": 1,
  "channel": "fab",
  "bridge": {
    "state": "ready",
    "endpoint": "unix:///path/to/loomle.sock",
    "runtimeRegistered": true,
    "projectRegistered": true,
    "projectId": "...",
    "projectRoot": "...",
    "uproject": "..."
  },
  "plugin": {
    "path": ".../LoomleBridge",
    "version": "0.6.0",
    "installScope": "engine",
    "managedBy": "fab",
    "hasFabPythonMcp": true
  },
  "nativeCli": {
    "detected": true,
    "path": "~/.loomle/bin/loomle",
    "version": "0.6.0",
    "configuredHosts": ["codex", "claude"]
  },
  "fabPythonMcp": {
    "available": true,
    "path": ".../LoomleBridge/Resources/MCP/loomle_mcp_server.py",
    "recommendedCommand": "uv",
    "config": {
      "command": "uv",
      "args": [
        "--directory",
        ".../LoomleBridge/Resources/MCP",
        "run",
        "loomle_mcp_server.py"
      ]
    }
  },
  "hosts": [
    {
      "id": "codex",
      "detected": true,
      "configPath": "~/.codex/config.toml",
      "loomleEntry": {
        "present": true,
        "owner": "native",
        "serverName": "loomle"
      },
      "canAutoConfigure": false,
      "reason": "nativeConfigured"
    },
    {
      "id": "claude",
      "detected": true,
      "configPath": "~/Library/Application Support/Claude/claude_desktop_config.json",
      "loomleEntry": {
        "present": false,
        "owner": null,
        "serverName": null
      },
      "canAutoConfigure": true,
      "reason": null
    }
  ],
  "recommendation": {
    "action": "keepNative",
    "message": "Native loomle is already configured and can connect to this Fab plugin.",
    "safeAutomaticActions": [],
    "warnings": []
  }
}
```

Stable enum values:

- `channel`: `native`, `fab`, `source`, or `unknown`.
- `bridge.state`: `ready`, `starting`, `offline`, `degraded`, or `pie`.
- `plugin.installScope`: `project`, `engine`, or `unknown`.
- `plugin.managedBy`: `native`, `fab`, `external`, or `unknown`.
- `hosts[].id`: initially `codex` and `claude`.
- `hosts[].loomleEntry.owner`: `native`, `fab`, `manual`, `unknown`, or `null`.
- `recommendation.action`: `keepNative`, `configureFabPython`,
  `showManualConfig`, `fixBridge`, or `noAction`.

Status rules:

- If a native MCP entry is already configured, recommend `keepNative`.
- If no native entry exists, Fab Python MCP is available, and a host config path
  is detected unambiguously, recommend `configureFabPython`.
- If no host config path is detected, recommend `showManualConfig`.
- If the bridge is not reachable or runtime registration is missing, recommend
  `fixBridge`.
- `setup.status` is read-only. Any config mutation must go through an explicit
  setup action that backs up and merges host config.

If Codex or Claude config already contains native `loomle`:

- Show: `Native loomle is already configured.`
- Recommendation: keep native.
- Explain: native `loomle mcp` can connect to this Fab plugin through the shared
  runtime registry and Bridge RPC.
- Do not configure Fab Python MCP unless the user explicitly asks for an
  advanced alternate server name such as `loomle-fab`.

If Codex or Claude is detected and no native LOOMLE entry exists:

- Back up the original config automatically.
- Merge the `loomle` MCP server entry without deleting other servers or user
  settings.
- Tell the user which host was configured and ask them to restart the AI tool.
- Prefer `uv --directory <PluginPath>/Resources/MCP run loomle_mcp_server.py`.
- Offer a plain `python3 <PluginPath>/Resources/MCP/loomle_mcp_server.py`
  fallback in docs for users who manage dependencies themselves.

If Codex or Claude cannot be detected:

- Show `Copy Setup Prompt`, not raw config snippets.
- Link to install/setup docs.
- Put plugin path, runtime endpoint, and project id under advanced details.

The setup panel should optimize for fewer decisions. Opening the panel is the
setup action: it may auto-write MCP host configuration only when the host and
config path are detected unambiguously and the write is safe. Otherwise it
should fall back to prompt + docs.

### Setup Configure Action

`setup.configure` is the explicit mutation paired with read-only
`setup.status`.

Input:

```json
{
  "host": "codex",
  "server": "auto"
}
```

Fields:

- `host`: `codex` or `claude`.
- `server`: `auto`, `fabPython`, or `native`.

`auto` means:

- keep native if native `loomle mcp` is already configured;
- otherwise use Fab Python MCP if this is a Fab/plugin-resource setup;
- otherwise use native `loomle mcp` when a native CLI is installed.

Safety rules:

- `setup.configure` must call the same status builder as `setup.status` before
  deciding.
- If `hosts[].canAutoConfigure` is false, return `CONFIGURATION_BLOCKED`
  instead of writing.
- If a `loomle` entry already exists, do not overwrite it in the first version.
- If native `loomle mcp` is configured, do not replace it with Fab Python MCP.
- Always write a timestamped backup next to the original config before editing.
- Merge only the `loomle` MCP server entry; preserve every unrelated MCP server
  and every unrelated user setting.
- Return the backup path, target config path, selected server owner, and whether
  the file changed.

Codex write shape:

```toml
[mcp_servers.loomle]
command = "uv"
args = ["--directory", "<PluginPath>/Resources/MCP", "run", "loomle_mcp_server.py"]
```

Native Codex shape:

```toml
[mcp_servers.loomle]
command = "~/.loomle/bin/loomle"
args = ["mcp"]
```

Claude behavior:

- Native setup should return the recommended
  `claude mcp add --scope user loomle -- <loomle> mcp` command instead of
  running an external CLI from the MCP process in the first version.
- Fab Python setup may write Desktop JSON only after the config path is detected
  unambiguously and the user confirms.
- If neither path is safe, return `MANUAL_CONFIG_REQUIRED` with copyable
  snippets.

Result:

```json
{
  "configured": true,
  "host": "codex",
  "serverOwner": "fab",
  "configPath": "~/.codex/config.toml",
  "backupPath": "~/.codex/config.toml.loomle-backup-20260520T120000Z",
  "changed": true,
  "message": "Configured Codex to use Fab Python MCP."
}
```

Error responses should be structured:

- `CONFIGURATION_BLOCKED`: status says this host is not safe to configure.
- `LOOMLE_ENTRY_EXISTS`: a `loomle` entry already exists and was not changed.
- `NATIVE_CONFIGURED`: native `loomle mcp` is already configured.
- `FAB_PYTHON_UNAVAILABLE`: Fab Python MCP files are missing.
- `NATIVE_CLI_UNAVAILABLE`: native CLI path or active install state is missing.
- `HOST_CONFIG_UNAVAILABLE`: host config directory or CLI is not detectable.
- `BACKUP_FAILED`: backup could not be written.
- `CONFIG_WRITE_FAILED`: merged config could not be written.

## Update Model

Website/GitHub installs keep using `loomle update`.

Fab installs need one clear rule:

- Fab owns the plugin version installed into the engine/project.
- Native `loomle` owns only the native CLI and native project-local plugin
  installs.

If native `project.install` detects a Fab/Engine-managed `LoomleBridge`, it
must not overwrite it. The native MCP server can still connect to the running
Fab plugin through `project.attach`.

The plugin should report:

- plugin source: `native`, `fab`, `external`, or `unknown`
- plugin install scope: `project` or `engine`
- plugin path
- plugin version
- MCP server owner: native CLI or Fab Python MCP
- version compatibility status

## Required Product Work

Before Fab submission:

1. Add a minimal in-editor LOOMLE setup/status panel.
2. Teach project status to recognize Engine Plugin/Fab installs.
3. Add a Fab package assembly script that embeds `mcp/python` under
   `Resources/MCP`.
4. Add package verification for the Python MCP runtime, shared tool manifest,
   and expected `Resources/MCP` layout.
5. Add docs for the Fab flow and explain how it relates to the website
   installer.
6. Verify the package with UE `BuildPlugin` for each supported engine/platform.

## Fab Technical Review Rules

The first Fab technical review made the package rules more concrete:

- Do not ship an empty `Content` directory when `CanContainContent` is false.
  Fab treats an empty content folder as unused content and expects real content
  to live under a named pack folder.
- `FilterPlugin.ini` must explicitly include every custom non-standard file or
  folder that should be distributed. Loomle includes `/Resources/MCP/...` and
  `/README.md`.
- `.uplugin` must include the intended `EngineVersion`.
- Every module entry must include a platform allow/deny list that matches the
  listing's supported target platforms.
- Source files must carry a copyright notice with publisher name and year.
- Any enabled Unreal plugin dependency that is visible in `.uplugin` must be
  either removed if unused or mentioned in the product page/technical details.
  Loomle uses `PythonScriptPlugin` for the editor-side `execute` runtime bridge
  and `PCG` for PCG graph tooling, so both should remain documented.

## Open Risks

- Fab may require additional review clarification for Python source and
  dependency metadata under plugin resources.
- Fab may require the Python MCP runtime to be documented as a developer tool
  dependency rather than an editor runtime feature.
- A Fab Engine Plugin install changes the meaning of `project.install`; the
  CLI must not overwrite a plugin that Fab manages.
- Automatic Claude/Codex config writes from inside UE may feel surprising. The
  panel must make this explainable by writing only after the user opens the
  setup panel, backing up first, preserving existing entries, and showing a
  clear result.

## Fallback

If the bundled Python MCP runtime is rejected, keep the same setup panel but
change the Fab Python MCP action to:

1. open <https://loomle.ai/install.html>, or
2. show native `loomle mcp` configuration instructions after explicit
   confirmation.

The Fab product still has value as the UE-side bridge and onboarding surface.
