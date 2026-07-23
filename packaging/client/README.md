# Client Executable Packaging

This directory turns the self-contained `client/dist/main.cjs` bundle into the
native Loomle program consumed by Fab staging and executable tests.

Build and test the program for the current native runner:

```sh
npm run build:executable
npm run test:executable
```

The accepted native QA targets and canonical outputs are:

```text
.tmp/client/darwin-arm64/loomle
.tmp/client/darwin-arm64/build.json
.tmp/client/win32-x64/loomle.exe
.tmp/client/win32-x64/build.json
```

`build.json` schema version 3 records the product version, Client–Bridge
protocol version, native target, pinned Node version and runtime archive
SHA-256, executable name, and executable SHA-256. Fab assembly requires this
receipt and rechecks the executable bytes, so a receipt from another product or
protocol version, target, Node runtime, or executable is rejected. The receipt
is build provenance, not proof that unchanged versions have no newer source
edits; always build and test the Client immediately before assembly. The
receipt is not shipped in the plugin.

`node-runtime.json` pins the exact official Node.js 24 LTS archive and SHA-256.
The build verifies that archive, uses the extracted Node binary to generate the
SEA blob, and injects it into a copy of the same binary. The Mac QA executable
receives an ad-hoc local signature. The Windows QA executable remains unsigned.
Snapshot and code cache generation are disabled so the executable does not
capture build-machine state.

The ad-hoc Mac signature and unsigned Windows program are only for native QA.
Developer ID signing, notarization, and Windows Authenticode policy belong to
the later release decision.
