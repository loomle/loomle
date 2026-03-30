# LOOMLE Install Entrypoint Design

## Goal

Provide one homepage-driven install flow that is easy for both:

- a human opening `loomle.ai`
- an agent asked to install LOOMLE into the current Unreal project

## Core Rule

The first `0.4` install flow should describe:

- project-local install
- site-served install

It should not describe:

- global machine install
- temporary installer binary
- project-local update commands
- project-local doctor commands

## Visible Homepage Prompt

Recommended visible prompt:

```text
Install LOOMLE from loomle.ai
```

Supporting hint:

```text
Paste this into your coding agent from the Unreal project root.
```

## Agent-Facing Content

The page body should explicitly explain:

1. what LOOMLE installs into the project
2. how to run the install script
3. that installed projects keep their own maintenance scripts under `Loomle/`
4. which assets are published on the release page versus installed into the project

## Recommended Instruction Shape

### What LOOMLE installs

- `Plugins/LoomleBridge/`
- `Loomle/`

### Install

- macOS/Linux: `curl -fsSL https://loomle.ai/install.sh | bash -s -- --project-root /path/to/MyProject`
- Windows PowerShell:
  `& ([scriptblock]::Create((irm https://loomle.ai/install.ps1))) --project-root C:\Path\To\MyProject`

These bootstrap scripts are homepage entrypoints served by `loomle.ai`. They
are not copied into the installed project.

### Installed Project Maintenance

After install, the project keeps platform-specific maintenance scripts under
`Loomle/`:

- macOS/Linux: `Loomle/update.sh`, `Loomle/doctor.sh`
- Windows PowerShell: `Loomle/update.ps1`, `Loomle/doctor.ps1`

### Release Assets Versus Installed Files

Release page assets should include:

- `loomle-darwin.zip`
- `loomle-windows.zip`
- `loomle-manifest.json`
- `loomle-manifest-darwin.json`
- `loomle-manifest-windows.json`

Installed projects should keep:

- `Loomle/loomle(.exe)`
- `Loomle/update.sh`
- `Loomle/update.ps1`
- `Loomle/doctor.sh`
- `Loomle/doctor.ps1`

## Decision

The homepage/install-entrypoint design for the first `0.4` cut should teach:

- project-local installation
- site-served bootstrap
- installed projects keep their own maintenance scripts
- no installer-binary flow
