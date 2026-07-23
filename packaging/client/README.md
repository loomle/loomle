# Client Executable Packaging

This directory turns the self-contained `client/dist/main.cjs` bundle into the
native Loomle program consumed by Fab staging and executable tests.

Build and test the program for the current native runner:

```sh
npm run build:executable
npm run test:executable
```

The first accepted target is `darwin-arm64` and its canonical output is:

```text
.tmp/client/darwin-arm64/loomle
.tmp/client/darwin-arm64/build.json
```

`build.json` schema version 2 records the product version, Client–Bridge
protocol version, native target, pinned Node version and runtime archive
SHA-256, executable name, and executable SHA-256. Fab assembly requires this
receipt and rechecks the executable bytes, so a receipt from another product or
protocol version, target, Node runtime, or executable is rejected. The receipt
is build provenance, not proof that unchanged versions have no newer source
edits; always build and test the Client immediately before assembly. The
receipt is not shipped in the plugin.

`node-runtime.json` pins the exact official Node.js 24 LTS archive and SHA-256.
The build verifies that archive, uses the extracted Node binary to generate the
SEA blob, injects it into a copy of the same binary, and applies an ad-hoc local
signature. Snapshot and code cache generation are disabled so the executable
does not capture build-machine state.

The ad-hoc signature is only for local execution and smoke testing. Developer
ID signing, notarization, Windows Authenticode signing, license staging, and the
remaining native targets belong to the later release-assembly step.
