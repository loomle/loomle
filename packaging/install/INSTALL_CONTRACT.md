# LOOMLE Install Contract

## 1. Goal

LOOMLE installs once into the current user's global install root.

UE projects are prepared separately through MCP `project.install`, which installs
or updates project support from the global plugin cache.

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
6. create `state/runtimes`, `locks`, and `logs`
7. print Codex and Claude MCP configuration commands
8. avoid writing Codex or Claude config directly
9. be idempotent for repeated installs of the same version

## 6. Source-of-Truth Rule

The release manifest determines:

- which bundle is downloaded
- which binary path is the client payload
- where the plugin cache source lives inside the bundle

Project installation is an MCP responsibility, not a public bootstrap script
responsibility.
