# LOOMLE Install Page Content

## Purpose

This document defines the page-body content for the first `0.4` install model.

The page should explain:

- project-local install
- script-first bootstrap
- script-first update

It should not describe:

- global install
- temporary installer binary

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
curl -fsSL https://loomle.ai/install.sh | sh -s -- --project-root /path/to/MyProject

Windows PowerShell:
& ([scriptblock]::Create((irm https://loomle.ai/install.ps1))) -ProjectRoot C:\Path\To\MyProject
```

### 3. Update

```text
Update an existing LOOMLE project install with the platform update script.

macOS/Linux:
curl -fsSL https://loomle.ai/update.sh | sh -s -- --project-root /path/to/MyProject

Windows PowerShell:
& ([scriptblock]::Create((irm https://loomle.ai/update.ps1))) -ProjectRoot C:\Path\To\MyProject
```

### 4. Verify

```text
After installation or update, verify with:

Loomle/loomle doctor
```

## Agent Note

The page should explicitly state:

```text
Agents should treat this page as the canonical LOOMLE install instruction source.
The first 0.4 install model is project-local and script-first.
```

## Final Recommendation

Keep the page visually simple, but make the install/update commands explicit in
plain text so an agent can execute them directly without guessing.
