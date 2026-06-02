# LOOMLE Install Contract

## 1. Goal

LOOMLE installs once into the current user's global install root.

UE projects are prepared through the global plugin cache. MCP `project.install`
installs or updates an explicit project. `loomle update` also syncs registered
offline projects after the global install is updated.

## 2. Global Install Layout

After installation, the user install root should contain:

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
    projects/
    runtimes/
  locks/
  logs/
```

Windows uses the equivalent `%USERPROFILE%\.loomle` root.

## 3. Release Bundle Layout

Release bundles contain:

```text
loomle(.exe)
plugin-cache/
  LoomleBridge/
```

Release bundles must not contain the old per-project client workspace,
workflow guides, examples, or docs.

## 4. Project Support Layout

`project.install` materializes project support into:

```text
<ProjectRoot>/
  Plugins/
    LoomleBridge/
```

The target project should not receive a per-project `Loomle/` client
workspace in the global install model.

## 5. Installer Responsibilities

The public installer must:

1. download the release manifest and platform bundle
2. copy `loomle(.exe)` to `~/.loomle/versions/<version>/`
3. copy `plugin-cache/LoomleBridge/` to `~/.loomle/versions/<version>/plugin-cache/LoomleBridge/`
4. install the stable command at `~/.loomle/bin/loomle`
5. write `~/.loomle/install/active.json`
6. add `~/.loomle/bin` to the current user's PATH
7. create `state/projects`, `state/runtimes`, `locks`, and `logs`
8. configure Codex and Claude MCP hosts when their user config locations or CLIs are available
9. print a friendly installation summary with MCP configuration status and next steps
10. provide manual MCP configuration commands in next steps for hosts that were not configured
11. be idempotent for repeated installs of the same version

## 6. Source-of-Truth Rule

The release manifest determines:

- which bundle is downloaded
- which binary path is the client payload
- where the plugin cache source lives inside the bundle

Project installation is an MCP responsibility, not a public bootstrap script
responsibility.

## 7. Update Responsibilities

`loomle update` must:

1. update the global install and active plugin cache
2. read registered project records from `~/.loomle/state/projects`
3. sync the active `LoomleBridge` plugin into registered offline projects
4. skip online projects without overwriting loaded plugins
5. report updated, unchanged, skipped, and failed project counts

When syncing a LOOMLE-managed project plugin, `loomle update` should remove
project-local `Plugins/LoomleBridge/Binaries` build outputs. Unreal Editor owns
the platform binary rebuild for the current engine build, and stale
`UnrealEditor.modules` BuildId manifests must not be preserved across plugin
updates.

Project sync failures should be reported per project. They should not make the
global update fail after the global install has already succeeded.

## 8. Project Registry

LOOMLE stores persistent project registrations separately from live runtime
state:

- `~/.loomle/state/projects/<projectId>.json` records projects whose
  `LoomleBridge` installation is managed by LOOMLE.
- `~/.loomle/state/runtimes/<projectId>.json` records currently running Bridge
  instances and may be removed when Unreal Editor shuts down.

`project.install` must upsert the persistent project record after a successful
install or no-op version check. Bridge startup may also upsert the persistent
record to backfill projects that were installed before the registry existed.
Bridge shutdown must only remove the runtime record.
