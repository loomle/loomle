# Global Install State

LOOMLE keeps global install state under `~/.loomle/install/active.json`.
The stable launcher reads this file before normal CLI parsing so it can hand
off to the active versioned client.

## Design Intent

The file is a launcher contract, not a user-editable project setting. It must
stay small, deterministic, and readable by every supported LOOMLE launcher
version that may be left in `~/.loomle/bin`.

## Schema

Current writers emit schema version 2:

```json
{
  "schemaVersion": 2,
  "installedVersion": "0.5.37",
  "activeVersion": "0.5.37",
  "platform": "windows",
  "installRoot": "C:\\Users\\name\\.loomle",
  "launcherPath": "C:\\Users\\name\\.loomle\\bin\\loomle.exe",
  "activeClientPath": "C:\\Users\\name\\.loomle\\versions\\0.5.37\\loomle.exe",
  "versionsRoot": "C:\\Users\\name\\.loomle\\versions",
  "pluginCacheRoot": "C:\\Users\\name\\.loomle\\versions\\0.5.37\\plugin-cache"
}
```

Required fields for startup handoff:

- `activeVersion`
- `launcherPath`
- `activeClientPath`

`project.install`, `project.sync`, and registered project maintenance may read
`activeVersion`; for compatibility they may fall back to `installedVersion`.

## Encoding

Writers must emit UTF-8 JSON without a byte-order mark. A trailing newline is
allowed.

Readers must accept a leading UTF-8 BOM. Older Windows PowerShell installer
paths wrote `active.json` with a BOM, and the launcher must not fail before it
can update or hand off from that state.

## Error Responses

If the file is missing, startup continues without a handoff. If the file exists
but cannot be read, parsed, or does not contain the required handoff fields,
startup reports `ActiveInstallStateInvalid` and exits with the existing startup
error path.

## Audit

The active state contract maps directly to LOOMLE's global install model:

- the launcher path is the stable command path
- the active client path is the versioned executable to run
- the active version is the plugin cache version used for project support sync

LOOMLE-managed JSON readers should tolerate a leading UTF-8 BOM for files that
may have been written or edited through Windows text tooling. This includes
active install state, update cache, project/runtime registry records, project
plugin descriptors, recipe files, and lock metadata. LOOMLE-managed writers
should continue to emit UTF-8 without a BOM.

Regression coverage must include BOM input compatibility and no-BOM output from
local version switching.
