# LOOMLE Install Page Content

## Purpose

This document defines the page-body content for the first `0.4` install model.

The page should explain:

- project-local install
- site-served install

It should not describe:

- global install
- temporary installer binary
- project-local update commands
- project-local doctor commands

## Above-The-Fold

Visible line:

```text
Install LOOMLE from loomle.ai
```

Visible hint:

```text
Paste this into your coding agent from the Unreal project root.
```

## Required Body Content

### 1. What LOOMLE installs

```text
LOOMLE installs two things into an Unreal project:
- Plugins/LoomleBridge/
- Loomle/
```

### 2. Install

```text
Install LOOMLE into the current Unreal project with the platform script.

macOS/Linux:
curl -fsSL https://loomle.ai/install.sh | bash -s -- --project-root /path/to/MyProject

Windows PowerShell:
& ([scriptblock]::Create((irm https://loomle.ai/install.ps1))) -ProjectRoot C:\Path\To\MyProject
```

### 3. After Install

```text
After installation, the project keeps platform-specific maintenance scripts
under Loomle/ for later update and doctor work.
```

## Agent Note

The page should explicitly state:

```text
Agents should treat this page as the canonical LOOMLE install instruction source.
The first 0.4 install model is project-local and script-first.
The site publishes install scripts only. Installed projects keep their own
update/doctor scripts under Loomle/.
```

## Final Recommendation

Keep the page visually simple, but make the install commands explicit in plain
text so an agent can execute them directly without guessing.
