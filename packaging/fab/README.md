# Fab Packaging

Fab packaging builds a UE plugin-first payload. It does not include the native
`loomle` binary and does not replace the website/GitHub release bundle.

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
