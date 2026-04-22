# LOOMLE Repository Structure

## Summary

LOOMLE now uses a global client install plus per-project UE plugin support.

- Global install root: `~/.loomle`
- Global MCP command: `loomle mcp`
- UE project install target: `<ProjectRoot>/Plugins/LoomleBridge/`
- Runtime registry: `~/.loomle/state/runtimes/`

There is no installed per-project client workspace in the current model.

## Source Layout

### `client/`

Rust implementation of the global `loomle` command.

It owns:

- `loomle mcp`
- `loomle update`
- `loomle doctor`
- global install state handling
- per-session project attachment

### `engine/LoomleBridge/`

UE plugin source.

It owns:

- runtime registration under `~/.loomle/state/runtimes/`
- native MCP tool execution in the Unreal Editor process
- project-scoped IPC endpoint creation

### `packaging/`

Release and install infrastructure.

Release bundles contain:

```text
loomle(.exe)
plugin-cache/
  LoomleBridge/
```

### `site/`

Published website and bootstrap scripts:

- `https://loomle.ai/`
- `https://loomle.ai/install.sh`
- `https://loomle.ai/install.ps1`

The site scripts must stay aligned with `client/install.sh` and
`client/install.ps1`.

### `workspace/`

Legacy graph guide and example material retained in the repository for product
development and tests. It is not part of release bundles and is not installed
into user projects.

## Installed Global Shape

```text
~/.loomle/
  bin/
    loomle
  install/
    active.json
  versions/
    <version>/
      loomle
      plugin-cache/
        LoomleBridge/
  state/
    runtimes/
  locks/
  logs/
```

## Target Project Shape

```text
<ProjectRoot>/
  Plugins/
    LoomleBridge/
```

`project.install` installs or updates this plugin from the global plugin cache.
It should not create a per-project client workspace, install state, runtime
state, or update scripts.
