# LOOMLE Install Entrypoint Design

## Goal

Provide one homepage-driven install flow that is easy for both:

- a human opening `loomle.ai`
- an agent asked to install LOOMLE into the current Unreal project

## Core Rule

The first `0.4` install flow should describe:

- project-local install
- script-first bootstrap
- script-first update

It should not describe:

- global machine install
- temporary installer binary

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
3. how to run the update script
4. how to verify the install

## Recommended Instruction Shape

### What LOOMLE installs

- `Plugins/LoomleBridge/`
- `Loomle/`

### Install

- macOS/Linux: `curl -fsSL https://loomle.ai/install.sh | sh -s -- --project-root /path/to/MyProject`
- Windows PowerShell:
  `& ([scriptblock]::Create((irm https://loomle.ai/install.ps1))) -ProjectRoot C:\Path\To\MyProject`

### Update

- macOS/Linux: `curl -fsSL https://loomle.ai/update.sh | sh -s -- --project-root /path/to/MyProject`
- Windows PowerShell:
  `& ([scriptblock]::Create((irm https://loomle.ai/update.ps1))) -ProjectRoot C:\Path\To\MyProject`

### Verify

- `Loomle/doctor.sh --project-root /path/to/MyProject`
- `Loomle/doctor.ps1 -ProjectRoot C:\Path\To\MyProject`

## Decision

The homepage/install-entrypoint design for the first `0.4` cut should teach:

- project-local installation
- script bootstrap
- script update
- no installer-binary flow
