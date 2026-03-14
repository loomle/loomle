# LOOMLE Bootstrap Layer

This directory defines the first-run install entrypoints that let a machine with no existing LOOMLE setup acquire the `loomle` command.

Primary artifacts:

- `install.sh`
  - bootstrap entrypoint for macOS and Linux
- `install.ps1`
  - bootstrap entrypoint for Windows PowerShell
- `BOOTSTRAP_CONTRACT.md`
  - defines where the bootstrap scripts should live and what `loomle.ai/i` should point to

Bootstrap is distinct from project installation:

1. bootstrap installs the global `loomle` CLI on the user's machine
2. `loomle install` installs LOOMLE into a specific Unreal project
