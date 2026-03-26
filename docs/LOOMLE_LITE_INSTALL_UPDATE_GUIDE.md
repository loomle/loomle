# LOOMLE Lite Install and Update Guide

## Summary

`LOOMLE Lite` should be installable and updatable without a global installer,
bootstrap script, or required machine-wide command.

The intended model is:

- download an archive
- copy the project-local files into the Unreal project
- follow the written usage steps

This guide defines that lightweight contract.

## What LOOMLE Lite Ships

A LOOMLE Lite release should ship at least these project-facing parts:

- `Plugins/LoomleBridge/`
- `loomle/`

Inside `loomle/`, the important early subdirectories are:

- `loomle/skills/`
- `loomle/worklog/`

The product may also ship guides, references, examples, or other project-local
supporting files inside `loomle/`.

## Install Goal

After manual installation, a user project should look like:

```text
<ProjectRoot>/
  Plugins/
    LoomleBridge/
      ...

  loomle/
    skills/
    worklog/
    ...
```

## Manual Install Steps

### 1. Download the release archive

Get the LOOMLE release archive for the target version.

### 2. Extract the archive

Extract it somewhere local so the contained project directories are visible.

### 3. Copy the Unreal plugin

Copy:

- `Plugins/LoomleBridge/`

into the target Unreal project under:

- `<ProjectRoot>/Plugins/LoomleBridge/`

### 4. Copy the LOOMLE project-local directory

Copy:

- `loomle/`

into the target Unreal project root.

### 5. Confirm the project shape

Check that the Unreal project now contains:

- `Plugins/LoomleBridge/`
- `loomle/`

### 6. Restart Unreal Editor if needed

If the project was already open, restart Unreal Editor so the new plugin build
and runtime assets are picked up cleanly.

## Manual Update Philosophy

LOOMLE Lite should prefer a clear manual update contract over a heavy automatic
updater.

The user should know which parts are:

- machine-provided
- safe to refresh from a new archive
- user-maintained and should be preserved

## Ownership Rules For Manual Update

### Usually safe to refresh from a new archive

- `Plugins/LoomleBridge/`
- machine-provided files under `loomle/`
- shipped role skill definitions under `loomle/skills/`

### Preserve by default

- user-authored files under `loomle/worklog/`
- any user-maintained project notes kept under `loomle/`
- any locally adapted documents the user intends to keep

## Recommended Manual Update Steps

### 1. Download the new release archive

Get the new LOOMLE Lite version archive.

### 2. Extract it locally

Do not overwrite the project in place directly from the compressed archive.

### 3. Replace the plugin directory

Refresh:

- `<ProjectRoot>/Plugins/LoomleBridge/`

from the new release.

This is the most important machine-owned runtime surface.

### 4. Refresh machine-provided LOOMLE files

Refresh the shipped LOOMLE files under:

- `<ProjectRoot>/loomle/`

but preserve any user-maintained worklog or project notes.

### 5. Preserve worklog history

Do not delete:

- `<ProjectRoot>/loomle/worklog/`

unless the user explicitly wants to reset it.

### 6. Review local customizations

If the user has locally edited shipped skill files or guides, they should
decide whether to:

- keep their local version
- adopt the new release version
- manually merge the two

## Upgrade Guidance For Agents

When an agent helps with a LOOMLE Lite update, it should follow these rules:

- do not assume a global installer exists
- do not assume a `loomle install` command exists
- treat the release archive as the source package
- preserve user-authored worklog content
- avoid destructive replacement of user-maintained local files unless explicitly
  asked

## What This Guide Intentionally Avoids

This guide does not assume:

- global commands
- global skill registration
- automatic discovery
- automatic migration machinery

Those can be considered later if the kernel proves it needs them.

For now, the install and update story should stay manual, clear, and low-risk.
