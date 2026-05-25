# Fab Packaging

Fab packaging builds a UE plugin-first source payload. It does not include the
native `loomle` binary, platform `Binaries/`, or Unreal build outputs, and it
does not replace the website/GitHub release bundle.
The tag release workflow publishes it as `loomle-fab-plugin.zip`.

The staging plugin is assembled from:

- `engine/LoomleBridge`
- `mcp/python` copied to `Resources/MCP`
- `mcp/manifest` copied to `Resources/MCP/tool-manifest`

Build the source plugin package:

```bash
python3 packaging/fab/assemble_fab_plugin.py \
  --repo-root . \
  --output-dir /tmp/loomle-fab-package
```

The expected source package must keep:

```text
LoomleBridge/LoomleBridge.uplugin
LoomleBridge/Source/LoomleBridge/LoomleBridge.Build.cs
LoomleBridge/Resources/MCP/loomle_mcp_server.py
LoomleBridge/Resources/MCP/tool-manifest/manifest.json
```

It must not include:

```text
LoomleBridge/Binaries/
LoomleBridge/Intermediate/
LoomleBridge/Saved/
*.dll, *.dylib, *.so, *.exe, *.lib, *.pdb
```

Release automation:

- `.github/workflows/release-loomle-fab.yml` runs after `Finalize LOOMLE Native
  Release` completes successfully, or by explicit manual dispatch with a release tag.
- The workflow checks that native, bridge, and Python MCP versions match the tag.
- The workflow assembles the source plugin package, zips the `LoomleBridge/`
  directory, and uploads `loomle-fab-plugin.zip` to the tag release.
- The Fab asset is intentionally independent from `loomle-latest`; Fab/Epic
  owns review, installation, and update timing for the plugin channel.
