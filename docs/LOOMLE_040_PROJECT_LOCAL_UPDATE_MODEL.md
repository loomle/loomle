# LOOMLE 0.4.0 Project-Local Update Model

## Summary

The current project-local update model is:

- keep stable entrypoints under `Loomle/`
- place the real client payload under `Loomle/install/versions/<version>/`
- use `Loomle/install/active.json` as the source of truth for the active version
- update `Plugins/LoomleBridge/` and `Loomle/` as owned install areas

This avoids whole-directory replacement and avoids self-overwriting the active
client binary during update.

## Stable Entry Layer

Users, hosts, and agents should only need these stable paths:

- `Loomle/loomle(.exe)`
- `Loomle/update.(sh|ps1)`
- `Loomle/install/active.json`

`Loomle/loomle(.exe)` is the stable launcher. The real versioned client lives
under `Loomle/install/versions/<version>/`.

## Active Version Selection

`Loomle/install/active.json` should record at least:

- installed version
- active version
- platform
- stable launcher path
- active client payload path
- plugin root
- workspace root

The launcher resolves the active client from this file and hands off to it.

## Update Flow

The update path should:

1. resolve the target project root
2. download the manifest and bundle
3. stage the new client payload under `Loomle/install/versions/<version>/`
4. update owned plugin files under `Plugins/LoomleBridge/`
5. refresh stable maintenance files under `Loomle/`
6. switch `Loomle/install/active.json` to the new active version

It should not:

- delete all of `Loomle/`
- overwrite the currently running client binary in place
- require a temporary installer binary

## Ownership Rules

The updater owns:

- `Plugins/LoomleBridge/`
- stable entrypoints under `Loomle/`
- `Loomle/install/`

It may preserve:

- `Loomle/state/`

It should not treat tracked collaboration content as installer-owned state.

## Decision

For the current `0.4` cut:

- use versioned client payloads
- use `active.json` for active-version selection
- keep update project-local only
- keep install/update script-first
