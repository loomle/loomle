# LOOMLE Install Contract

## 1. Goal

LOOMLE installs into a user project in one step.

One installation must place:

1. the Unreal plugin
2. the project-local workspace directory
3. the project-local Rust client
4. the platform-required runtime bridge binaries

## 2. Installed Project Layout

After installation, the user project should contain:

```text
<ProjectRoot>/
  Plugins/
    LoomleBridge/
      Binaries/
      Resources/
      Tools/
        mcp/
          linux/loomle_mcp_server
          windows/loomle_mcp_server.exe

  Loomle/
    README.md
    loomle(.exe)
    workflows/
    examples/
    runtime/
```

## 3. Placement Rules

### Plugin install target

Source bundle content:

```text
plugin/LoomleBridge/
```

Install destination:

```text
<ProjectRoot>/Plugins/LoomleBridge/
```

This target owns:
- Unreal plugin files
- bridge runtime assets
- platform runtime bridge binaries under `Tools/mcp/` when required by that platform

### Workspace install target

Source bundle content:

```text
workspace/Loomle/
```

Install destination:

```text
<ProjectRoot>/Loomle/
```

This target owns:
- Rust client entrypoint
- one Agent-facing README entrypoint
- workflow guides and small examples
- machine-written runtime state under `runtime/`

## 4. Runtime Ownership

### Platform runtime bridge binary

Installed at when required:

```text
<ProjectRoot>/Plugins/LoomleBridge/Tools/mcp/<platform>/loomle_mcp_server(.exe)
```

Reason:
- version-locked with the Unreal plugin
- bridge-coupled runtime component
- optional on platforms where `LoomleBridge` serves MCP directly

### Rust client

Installed at:

```text
<ProjectRoot>/Loomle/loomle(.exe)
```

Reason:
- project-local user and agent entrypoint
- workspace-layer runtime component

## 5. Installer Responsibilities

An installer must:

1. copy plugin content to `Plugins/LoomleBridge/`
2. copy workspace content to `Loomle/`
3. preserve platform-appropriate runtime bridge binaries and client binaries
4. ship release bundles with plugin `Source/` alongside prebuilt binaries so Unreal can participate in local target rebuilds
5. write `Loomle/runtime/install.json` with machine-readable install state
6. ensure `Config/DefaultEditorSettings.ini` disables background CPU throttling for Unreal Editor
7. avoid requiring a separate skill repository install
8. be idempotent for repeated installs of the same version

Source-checkout helper entrypoints:

- `packaging/release/build_local_release.py`
- `packaging/install/install_from_checkout.py`

## 6. Source-of-Truth Rule

The release manifest determines:
- which bundle is downloaded
- how plugin content is installed
- how workspace content is installed

Packaging scripts must treat the manifest as the source of truth for install destinations.
