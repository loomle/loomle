# Fab Packaging

Fab packaging assembles the UE Bridge source and one matching standalone
TypeScript Client executable into a platform-specific plugin payload. The
assembler does not build the Client or Unreal binaries. Its only Client input
is the canonical executable produced and tested by `packaging/client`:

```text
.tmp/client/<platform-arch>/loomle(.exe)
.tmp/client/<platform-arch>/build.json
```

The adjacent build receipt binds that executable to the current product
version, native target, pinned Node runtime archive, and SHA-256. It is verified
during assembly but is not copied into the plugin. Local QA must still build
and test the Client immediately before assembly because the receipt does not
fingerprint the entire source checkout.

The staged plugin is assembled from:

- `engine/LoomleBridge` for the Unreal source plugin;
- `.tmp/client/<platform-arch>/loomle(.exe)` for the standalone Client;
- `packaging/fab/FAB_PLUGIN_README.md` for the packaged README.

The Client is copied to
`LoomleBridge/Resources/Loomle/<platform-arch>/loomle(.exe)`. No alternative
Client implementation or resource tree is consumed. The package has no
`Content/` directory because `LoomleBridge` is an editor-only code plugin with
`CanContainContent=false`.

Only `darwin-arm64` is currently accepted. Assembly narrows
the derived plugin descriptor to `SupportedTargetPlatforms = ["Mac"]`, the
`LoomleBridge` module to `PlatformAllowList = ["Mac"]`, and
`PlatformArchitectureAllowList = ["Mac:arm64"]`. The source descriptor remains
multi-platform development input; accepting another release target requires a
new explicit target specification and its native verification path.

Build and test the canonical Client before assembling the Fab plugin:

```sh
npm run build:executable
npm run test:executable
```

Then assemble one target:

```bash
node packaging/fab/assemble.mjs \
  --repo-root . \
  --output-dir /tmp/loomle-fab-package \
  --target darwin-arm64
```

The assembler staging tree contains:

```text
LoomleBridge/LoomleBridge.uplugin
LoomleBridge/README.md
LoomleBridge/Config/FilterPlugin.ini
LoomleBridge/Source/LoomleBridge/LoomleBridge.Build.cs
LoomleBridge/Resources/Loomle/<platform-arch>/loomle(.exe)
```

Before UE compilation it must not include:

```text
LoomleBridge/Binaries/
LoomleBridge/Intermediate/
LoomleBridge/Saved/
LoomleBridge/Content/
```

Platform binaries and Unreal build outputs are rejected everywhere except for
the one exact Client executable path. The staged descriptor version and Client
build receipt are checked against the root product version, the receipt
SHA-256 is checked against both source and staged Client bytes, target fields
are checked against the accepted target, and `FilterPlugin.ini` must explicitly
keep itself and `Resources/Loomle`.

UE BuildPlugin consumes that staging tree and produces the distributable
plugin. The final tree must add the matching
`Binaries/Mac/UnrealEditor-LoomleBridge.dylib`, mark the descriptor
`Installed=true`, retain `Config/FilterPlugin.ini`, and preserve the exact
Client bytes and executable permission. For `darwin-arm64`, both the Bridge and
Client are arm64-only.

Run the assembler tests locally:

```sh
node --test packaging/fab/assemble.test.mjs
```

Release automation must run the Client build and executable smoke test on a
native target runner before invoking the assembler. UE BuildPlugin then compiles
that staged source for the same architecture. The distributable archive is the
BuildPlugin output, not the pre-build staging tree. The assembler deliberately
has no fallback input path.
