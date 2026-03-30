# LOOMLE Install Entrypoint Design

## Goal

Provide one homepage-driven install flow that is easy for both:

- a human opening `loomle.ai`
- an agent asked to install LOOMLE into the current Unreal project

## Current Rules

The current install flow should describe:

- project-local install
- site-served install scripts
- installed project-local update scripts
- release-manifest and release-zip based bootstrap

It should not describe:

- global machine install
- temporary installer binaries
- extra maintenance commands beyond the installed update scripts

## Homepage Prompt

Recommended visible prompt:

```text
Install LOOMLE from loomle.ai
```

Supporting hint:

```text
Paste this into your coding agent from the Unreal project root.
```

## Required Page Content

The page body should explicitly explain:

1. what LOOMLE installs into the project
2. how to run the install script on macOS/Linux and Windows
3. that `loomle.ai` publishes install scripts only
4. that installed projects keep update scripts under `Loomle/`
5. which release assets back the install flow

## Recommended Instruction Shape

### What LOOMLE installs

LOOMLE installs two owned areas into one Unreal project:

- `Plugins/LoomleBridge/`
- `Loomle/`

### Install Commands

- macOS/Linux: `curl -fsSL https://loomle.ai/install.sh | bash -s -- --project-root /path/to/MyProject`
- Windows PowerShell:
  `& ([scriptblock]::Create((irm https://loomle.ai/install.ps1))) --project-root C:\Path\To\MyProject`

These bootstrap scripts are homepage entrypoints served by `loomle.ai`. They
materialize project content directly from the release manifest and platform zip.
They are not copied into the installed project.

### After Install

Installed projects keep stable project-local entrypoints under `Loomle/`:

- `Loomle/loomle(.exe)`
- macOS/Linux: `Loomle/update.sh`
- Windows PowerShell: `Loomle/update.ps1`

### Release Assets Behind The Install Flow

The bootstrap scripts should resolve release assets like:

- `loomle-darwin.zip`
- `loomle-windows.zip`
- `loomle-manifest.json`
- `loomle-manifest-darwin.json`
- `loomle-manifest-windows.json`

### Agent Note

The page should make this explicit:

```text
Agents should treat this page as the canonical LOOMLE install instruction source.
LOOMLE installs directly into the current Unreal project.
The site publishes install scripts only. Installed projects keep their own
update scripts under Loomle/.
```

## Decision

The homepage/install-entrypoint design should teach:

- project-local installation
- site-served bootstrap
- release-manifest driven install
- installed projects keep their own update scripts
- no installer-binary flow
