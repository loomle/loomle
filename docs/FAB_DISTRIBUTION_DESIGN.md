# Fab Distribution Design

## Intent

LOOMLE should be publishable on Fab as a complete Unreal-facing product while
keeping the existing global installer path.

The Fab package should make first install easier for Unreal users:

1. Install the LOOMLE UE plugin from Fab.
2. Enable the plugin in an Unreal project.
3. Use an in-editor setup panel to install or activate the bundled `loomle`
   MCP CLI.
4. Configure Claude Code, Codex, or another MCP host to run `loomle mcp`.
5. Attach the running Unreal project.

The goal is one product with two installation entrypoints:

- Website/GitHub release installer: installs the global CLI first, then
  installs project support with `project.install`.
- Fab installer: installs the UE plugin first, then installs or activates the
  bundled CLI from inside Unreal.

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
- The bundled CLI must be documented as a LOOMLE runtime payload, not as a
  replacement for the UE plugin.
- If Fab rejects embedded executables in plugin resources, the fallback is to
  ship the plugin only and have the setup panel download the signed GitHub
  release bundle or open the website installer.

## Package Shape

Fab should receive a package rooted at the UE plugin folder.

The lower-risk submission shape treats the LOOMLE CLI as a plugin runtime
dependency that LOOMLE owns, not as an arbitrary extra executable hidden in
`Resources/`:

```text
LoomleBridge/
  LoomleBridge.uplugin
  Config/
  Source/
    LoomleBridge/
      LoomleBridge.Build.cs
      Public/
      Private/
    ThirdParty/
      LoomleCli/
        manifest.json
        darwin/
          loomle
        windows/
          loomle.exe
  Resources/
    Icon128.png
    install.md
```

The Fab package is still one UE plugin. The CLI is a runtime dependency inside
the plugin source package, not a second top-level product folder.

`PreBuildSteps` or build rules may copy the platform CLI payload to a packaged
runtime location such as:

```text
LoomleBridge/
  Binaries/
    ThirdParty/
      LoomleCli/
        <platform>/
          loomle(.exe)
```

`Config/FilterPlugin.ini` must preserve any packaged runtime dependency folder
that Fab's BuildPlugin step would otherwise discard.

Putting executables directly under `Resources/Fab/bin` is not the preferred
submission design. It may work technically, but it is harder to justify under
Fab's Code Plugin expectations than `Source/ThirdParty` plus explicit build
packaging.

The existing GitHub release bundle remains:

```text
loomle(.exe)
plugin-cache/
  LoomleBridge/
```

Both package shapes should be built from the same release inputs:

- `engine/LoomleBridge`
- `client/target/release/loomle`
- `client/target/release/loomle.exe`
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

The setup panel should support these states:

- `Bridge Ready`: plugin loaded and local RPC endpoint available.
- `CLI Installed`: `~/.loomle/bin/loomle` exists and active state is valid.
- `CLI Missing`: no global CLI installed.
- `Bundled CLI Available`: the Fab plugin contains a compatible CLI payload for
  the current platform.
- `MCP Host Configured`: Codex or Claude user config contains the LOOMLE MCP
  entry.
- `Project Attached`: current MCP session has attached to this runtime.

## Fab CLI Activation

The Fab setup panel should copy the bundled CLI into the same global layout used
by the website installer:

```text
~/.loomle/
  bin/
    loomle
  install/
    active.json
  versions/
    <version>/
      loomle
      manifest.json
      plugin-cache/
        LoomleBridge/
  state/
  locks/
  logs/
```

For Fab installs, `plugin-cache/LoomleBridge` should point to or copy the active
Fab plugin package so that existing `project.install` and `loomle update`
semantics still have a known plugin source.

The setup panel should prefer explicit user action:

- `Install LOOMLE CLI`
- `Configure Codex`
- `Configure Claude`
- `Copy MCP Config`
- `Open Install Guide`

It should not silently modify shell profiles or MCP host config on editor
startup.

## Update Model

Website/GitHub installs keep using `loomle update`.

Fab installs need one clear rule:

- Fab owns the plugin version installed into the engine/project.
- `loomle update` owns the global CLI and GitHub release payload.

If the user installed from Fab, `loomle update` should not overwrite the
Fab-installed Engine Plugin. It may update the global CLI, but project plugin
sync should be skipped or clearly marked as website-managed only.

The plugin should report:

- plugin source: `fab`, `projectInstall`, or `unknown`
- plugin path
- plugin version
- CLI version
- version compatibility status

## Required Product Work

Before Fab submission:

1. Add a minimal in-editor LOOMLE setup/status panel.
2. Teach project status to recognize Engine Plugin/Fab installs.
3. Add a Fab package assembly script that embeds platform CLI payloads under
   `Resources/Fab/`.
4. Add a Fab manifest with version, platform payloads, sha256, and expected
   global install layout.
5. Add docs for the Fab flow and explain how it relates to the website
   installer.
6. Verify the package with UE `BuildPlugin` for each supported engine/platform.

## Open Risks

- Fab may reject bundled executable payloads inside plugin resources.
- Fab may require executable payloads to be treated as plugin runtime
  dependencies under `Source/ThirdParty`, with proof of permission and explicit
  packaging rules.
- Fab may require separate platform packages or may build only UE plugin
  binaries, not arbitrary CLI payloads.
- A Fab Engine Plugin install changes the meaning of `project.install`; the
  CLI must not overwrite a plugin that Fab manages.
- Automatic Claude/Codex config writes from inside UE may feel surprising. The
  first Fab version should favor explicit buttons and copyable config.

## Fallback

If embedded CLI payloads are rejected, keep the same setup panel but change the
`CLI Missing` action to:

1. open <https://loomle.ai/install.html>, or
2. download the official GitHub release bundle after explicit confirmation.

The Fab product still has value as the UE-side bridge and onboarding surface.
