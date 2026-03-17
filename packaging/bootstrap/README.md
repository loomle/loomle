# LOOMLE Bootstrap Layer

This directory defines the first-run install entrypoints that let a machine with no existing LOOMLE setup acquire and execute a temporary `loomle-installer`.

Primary artifacts:

- `install.sh`
  - bootstrap entrypoint for macOS
  - exits early on Linux until Linux bootstrap artifacts exist
- `install.ps1`
  - bootstrap entrypoint for Windows PowerShell
- `BOOTSTRAP_CONTRACT.md`
  - defines where the bootstrap scripts should live and what `loomle.ai/i` should point to

Bootstrap is distinct from project installation:

1. bootstrap downloads and runs a temporary `loomle-installer`
2. `loomle-installer install` installs LOOMLE into a specific Unreal project
3. the temporary installer is removed after execution
