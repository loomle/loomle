# Packaging Layer

This directory owns the Loomle 0.7 executable and Fab artifact path.

Current responsibilities:

- `client/`: turn the self-contained TypeScript Client bundle into a native
  standalone program at `.tmp/client/<platform-arch>/loomle(.exe)`;
- `fab/`: assemble target-specific native QA fragments, verify each
  BuildPlugin result derives from its matching source, then merge the verified
  Mac and Windows fragments into one cross-platform source package and one
  cross-platform compiled plugin without source or binary drift;
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
  -> native QA fragments
  -> one cross-platform LoomleBridge source package
  -> one cross-platform compiled LoomleBridge package
```

The packaged Client contains SAL, Interfaces, MCP support, and its runtime. A
release does not depend on Rust, Python MCP, `uv`, a global Loomle installation,
or a project-local Client copy.

`darwin-arm64` and `win32-x64` have native QA paths. A target becomes releasable
only after its executable builder, isolated MCP smoke test, Fab assembly, UE
BuildPlugin verification, packaged end-to-end, signing policy, and promotion
contract are explicitly accepted. Passing QA alone does not advertise a
release target.
