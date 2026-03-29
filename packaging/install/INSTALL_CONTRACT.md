# LOOMLE Install Contract

## 1. Goal

LOOMLE installs into a user project in one step.

One installation must place:

1. the Unreal plugin
2. the project-local `Loomle/` directory
3. the project-local client binary

## 2. Installed Project Layout

After installation, the user project should contain:

```text
<ProjectRoot>/
  Plugins/
    LoomleBridge/
      Binaries/
      Resources/

  Loomle/
    README.md
    loomle(.exe)
    update.(sh|ps1)
    doctor.(sh|ps1)
    workflows/
    examples/
    install/
    state/
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

### LOOMLE install target

Source bundle content:

```text
Loomle/
```

Install destination:

```text
<ProjectRoot>/Loomle/
```

This target owns:
- Rust client entrypoint
- platform-specific installed maintenance scripts
- one Agent-facing README entrypoint
- workflow guides and small examples
- install metadata under `install/`
- machine-written runtime state under `state/`

## 4. Runtime Ownership

### Rust client

Installed at:

```text
<ProjectRoot>/Loomle/loomle(.exe)
```

Reason:
- project-local user and agent entrypoint
- project-local runtime component

## 5. Installer Responsibilities

An installer must:

1. copy plugin content to `Plugins/LoomleBridge/`
2. copy workspace content to `Loomle/`
3. preserve the project-local client binary
4. ship release bundles with plugin `Source/` alongside prebuilt binaries so Unreal can participate in local target rebuilds
5. write `Loomle/install/active.json` with machine-readable install state
6. ensure `Config/DefaultEditorSettings.ini` disables background CPU throttling for Unreal Editor
7. avoid requiring a separate skill repository install
8. be idempotent for repeated installs of the same version
9. keep the public install and installed update paths shell/PowerShell-only, without Python or an internal bundle helper

Source-checkout helper entrypoints:

- `packaging/release/build_local_release.py`
- `packaging/install/install_from_checkout.py`

## 6. Source-of-Truth Rule

The release manifest determines:
- which bundle is downloaded
- how plugin content is installed
- how `Loomle/` content is installed

Packaging scripts must treat the manifest as the source of truth for install destinations.
