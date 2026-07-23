# Packaging Layer

This directory owns the Loomle 0.7 executable and Fab artifact path.

Current responsibilities:

- `client/`: turn the self-contained TypeScript Client bundle into a native
  standalone program at `.tmp/client/<platform-arch>/loomle(.exe)`;
- `fab/`: combine the UE Bridge source with exactly one matching standalone
  Client under `Resources/Loomle/<platform-arch>/`;
- `tools/`: derive and verify the product and Client–Bridge protocol versions
  from the root `package.json`;
- `release/`: document release promotion and the currently accepted targets.

The canonical local path is:

```text
npm Client build
  -> client/dist/main.cjs
  -> packaging/client
  -> .tmp/client/<platform-arch>/loomle(.exe)
  -> packaging/fab
  -> staged LoomleBridge plugin
```

The packaged Client contains SAL, Interfaces, MCP support, and its runtime. A
release does not depend on Rust, Python MCP, `uv`, a global Loomle installation,
or a project-local Client copy.

`darwin-arm64` and `win32-x64` have native QA paths. A target becomes releasable
only after its executable builder, isolated MCP smoke test, Fab assembly, UE
BuildPlugin verification, packaged end-to-end, signing policy, and promotion
contract are explicitly accepted. Passing QA alone does not advertise a
release target.
