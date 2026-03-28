# LOOMLE Bootstrap Layer

This directory defines the first-run install entrypoints that let a machine with
no existing LOOMLE setup materialize LOOMLE directly into a project from a
release bundle and manifest.

Primary artifacts:

- `install.sh`
  - bootstrap entrypoint for macOS
  - exits early on Linux until Linux bootstrap artifacts exist
- `install.ps1`
  - bootstrap entrypoint for Windows PowerShell
- `update.sh`
  - project-local update bootstrap entrypoint for macOS/Linux
- `update.ps1`
  - project-local update bootstrap entrypoint for Windows PowerShell
- `BOOTSTRAP_CONTRACT.md`
  - defines where the bootstrap scripts should live and what `loomle.ai/i` should point to

Release publication model:

- tag releases publish `install.sh`, `install.ps1`, `update.sh`, and `update.ps1`
- installed projects keep `Loomle/update.*` and `Loomle/doctor.*`
- no `loomle-installer` binary should exist in the release model

Bootstrap is distinct from project installation:

1. bootstrap resolves the target Unreal project root
2. bootstrap downloads or locates a release bundle and manifest
3. bootstrap materializes `Plugins/LoomleBridge/` and `Loomle/`
4. no temporary installer binary is involved
