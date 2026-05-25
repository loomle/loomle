# Fab Packaging

Fab packaging builds a UE plugin-first payload. It does not include the native
`loomle` binary and does not replace the website/GitHub release bundle.
The tag release workflow publishes it as `loomle-fab-plugin.zip`.

The staging plugin is assembled from:

- `engine/LoomleBridge`
- `mcp/python` copied to `Resources/MCP`
- `mcp/manifest` copied to `Resources/MCP/tool-manifest`

Build the staging plugin:

```bash
python3 packaging/fab/assemble_fab_plugin.py \
  --repo-root . \
  --output-dir /tmp/loomle-fab-staging
```

Then pass the staged descriptor to Unreal `BuildPlugin`:

```bash
RunUAT BuildPlugin \
  -Plugin=/tmp/loomle-fab-staging/LoomleBridge/LoomleBridge.uplugin \
  -Package=/tmp/loomle-fab-package \
  -TargetPlatforms=Mac \
  -Rocket
```

The expected packaged plugin must keep:

```text
LoomleBridge/Resources/MCP/loomle_mcp_server.py
LoomleBridge/Resources/MCP/tool-manifest/manifest.json
```

Release automation:

- `.github/workflows/release-loomle-fab.yml` runs after `Finalize LOOMLE Release`
  completes successfully, or by explicit manual dispatch with a release tag.
- The workflow checks that native, bridge, and Python MCP versions match the tag.
- The workflow stages the plugin, runs Unreal `BuildPlugin`, zips the packaged
  `LoomleBridge/` directory, and uploads `loomle-fab-plugin.zip` to the tag release.
- The Fab asset is intentionally independent from `loomle-latest`; Fab/Epic
  owns review, installation, and update timing for the plugin channel.
