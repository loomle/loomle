# LOOMLE Bootstrap Contract

## Goal

Bootstrap starts from a machine with no LOOMLE install and creates a global
LOOMLE installation that MCP hosts can run with `loomle mcp`.

It does not install into a specific Unreal project. Project support is installed
later through the MCP tool `project.install`.

## Public Entrypoints

Published script entrypoints:

- `https://loomle.ai/install.sh`
- `https://loomle.ai/install.ps1`

Repository source:

- `client/install.sh`
- `client/install.ps1`
- `site/install.sh`
- `site/install.ps1`

## Bootstrap Responsibilities

Bootstrap scripts should:

1. detect platform
2. download or locate the release manifest and platform bundle
3. verify bundle checksum
4. create the global install root
5. install `bin/loomle`
6. install `versions/<version>/loomle`
7. install `versions/<version>/plugin-cache/LoomleBridge`
8. write `install/active.json`
9. add the global `bin` directory to the current user's PATH
10. create `state/runtimes`, `locks`, and `logs`
11. print MCP host configuration hints

## Artifact Contract

Release assets:

- `loomle-<version>-<platform>.zip`
- `manifest.json`

The bundle must contain:

- `loomle` or `loomle.exe`
- `plugin-cache/LoomleBridge/`

The bundle must not contain the old per-project client workspace.

## Update

Global updates are handled by:

```bash
loomle update
```

There are no per-project update scripts.

## Decision

The bootstrap contract is global install only:

- no project root argument
- no per-project client
- no per-project update scripts
- no daemon
