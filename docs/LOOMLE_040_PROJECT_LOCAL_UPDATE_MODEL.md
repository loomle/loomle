# LOOMLE 0.4.0 Project-Local Update Model

## Summary

For the first `LOOMLE 0.4.0` cut, project-local update should avoid two bad
patterns:

- whole-directory replacement of `Loomle/`
- in-place self-overwrite of the currently running updater or client binary

The recommended model is:

- stable project-local entrypoints in `Loomle/`
- versioned client payloads under `Loomle/install/versions/`
- manifest-driven active-version switching
- file-owned plugin updates under `Plugins/LoomleBridge/`

This gives `LOOMLE` a comfortable update story on both macOS and Windows
without introducing a global install model.

## Problem

The current simple update shape is workable on Unix-like systems, but fragile in
general and especially weak on Windows.

If `Loomle/update.ps1` runs from the installed project and the update process
tries to:

1. delete `Loomle/`
2. copy a new `Loomle/` back into place

then it risks colliding with:

- the currently running `update.ps1`
- the currently running `loomle.exe`
- PowerShell or Windows file locking behavior

Even when this sometimes works, it is not a comfortable or defendable design.

## Goals

The first `0.4` project-local update model should:

- remain project-local only
- avoid self-overwrite during update
- make rollback easy
- keep the installed entrypoint simple for humans and agents
- keep runtime/client ownership explicit

It should not in this phase:

- introduce global machine install
- introduce a heavy installer service
- introduce hidden machine-wide state

## Installed Shape

The installed project should evolve to this shape:

```text
<ProjectRoot>/
  Plugins/
    LoomleBridge/

  Loomle/
    loomle(.exe)
    update.(sh|ps1)
    doctor.(sh|ps1)
    README.md
    install/
      active.json
      versions/
        0.4.0/
          loomle(.exe)
```

The critical distinction is:

- `Loomle/loomle(.exe)` is a stable launcher entrypoint
- `Loomle/install/versions/<version>/loomle(.exe)` is the versioned payload

This lets update add a new version without replacing the currently executing
binary in place.

## Core Design

### 1. Stable Entry Layer

The following paths should be treated as stable project-local entrypoints:

- `Loomle/loomle(.exe)`
- `Loomle/update.(sh|ps1)`
- `Loomle/doctor.(sh|ps1)`
- `Loomle/install/active.json`

These are the only paths a user, host, or agent should need to know.

### 2. Versioned Client Payload

The real client binary should live under:

- `Loomle/install/versions/<version>/loomle(.exe)`

On upgrade, the updater should:

1. download and unpack the new release
2. materialize the new version directory
3. update install state so the stable launcher points at the new version

It should not try to replace the currently running versioned executable in
place.

### 3. Active Version Switch

`Loomle/install/active.json` should become the source of truth for:

- installed version
- active version
- platform
- plugin root
- workspace root
- stable launcher path
- active client payload path

The stable launcher should resolve the active version from install state and
exec that versioned payload.

This gives `LOOMLE`:

- atomic client cutover semantics
- simple rollback semantics
- no need for binary self-replacement tricks

### 4. Plugin Update Model

`Plugins/LoomleBridge/` should not follow the same versioned-directory model.

For the plugin, the correct first-cut strategy is:

- manifest-driven file ownership
- controlled overwrite of owned files
- explicit removal of plugin-owned files that no longer exist in the new bundle

It should avoid:

- deleting the entire plugin directory first
- treating user-owned files outside the owned set as mutable

## Update Flow

The recommended update flow is:

1. resolve project root
2. download manifest and bundle
3. unpack to temp
4. stage new `Loomle/install/versions/<version>/`
5. update plugin owned files under `Plugins/LoomleBridge/`
6. update stable project-local maintenance files that are safe to replace
7. atomically switch `active.json` to the new active version
8. leave previous client version in place for rollback/cleanup

What it must not do:

- delete all of `Loomle/`
- overwrite the currently running client binary
- require a temporary downloaded installer binary

## Why This Is Better

This model is the most comfortable first-cut solution for `LOOMLE` because it
balances simplicity and correctness.

Compared with whole-directory replacement, it gives:

- better Windows behavior
- clearer rollback
- safer partial failures
- fewer lock/file-use problems

Compared with ad hoc file-by-file overwrite, it gives:

- an explicit client version boundary
- a stable entrypoint that never has to know release internals
- simpler reasoning about which files may be in use

## Deferred Cleanup

Old version directories do not need aggressive cleanup in the first cut.

The initial policy can be:

- keep the current active version
- keep the previous version
- optionally remove older inactive versions later

That cleanup policy can be added after the basic update model is stable.

## Decision

For the first `LOOMLE 0.4.0` project-local update design:

- do not use whole-directory replacement for `Loomle/`
- do not self-overwrite the active client binary
- use `Loomle/install/versions/<version>/` for the real client payload
- use `Loomle/install/active.json` to select the active version
- update `Plugins/LoomleBridge/` through manifest-owned file replacement
