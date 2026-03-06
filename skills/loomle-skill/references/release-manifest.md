# Release Manifest

Use this schema for release-based Loomle project install.

## Goals

- Provide one online source of truth for stable versions.
- Keep install deterministic with checksum verification.
- Keep platform-specific packages explicit (`windows`, `darwin`, `linux`).

## JSON Schema (Practical)

```json
{
  "latest": "1.2.3",
  "versions": {
    "1.2.3": {
      "released_at": "2026-03-06T10:00:00Z",
      "min_ue": "5.7",
      "notes": "optional",
      "packages": {
        "darwin": {
          "url": "https://example.com/loomle/1.2.3/loomlebridge-darwin.zip",
          "sha256": "...",
          "format": "zip"
        },
        "linux": {
          "url": "https://example.com/loomle/1.2.3/loomlebridge-linux.tar.gz",
          "sha256": "...",
          "format": "tar.gz"
        },
        "windows": {
          "url": "https://example.com/loomle/1.2.3/loomlebridge-windows.zip",
          "sha256": "...",
          "format": "zip"
        }
      }
    }
  }
}
```

## Package Layout

Archive root must include `LoomleBridge/` directory.

Example:

```text
LoomleBridge/
  LoomleBridge.uplugin
  Binaries/
  Content/
  Resources/
```

The installer writes to:

`<ProjectRoot>/Loomle/Plugins/LoomleBridge`

## Validation Rules

- `latest` must exist in `versions`.
- Every package entry requires `url` and `sha256`.
- `sha256` must be lowercase hex.
- `format` should be `zip`, `tar.gz`, or `tgz`.
- Never publish mutable files at the same version URL.

## Caching Rules

- macOS/Linux: `~/.cache/loomle/releases`
- Windows: `%LOCALAPPDATA%/Loomle/releases`

Cache is a download cache only; install target remains project-local.
