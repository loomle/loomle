# LOOMLE Bootstrap Contract

## Goal

Bootstrap should start from a machine with no existing LOOMLE install and
materialize LOOMLE into a specific Unreal project.

The first `0.4` bootstrap direction is:

- script-first
- project-local
- no downloaded installer binary

## Public Entrypoints

Stable public script entrypoints should remain:

- `https://loomle.ai/install.sh`
- `https://loomle.ai/install.ps1`

These scripts target a specific Unreal project root.

Their source-of-truth in the repository should live under:

- `client/install.sh`
- `client/install.ps1`
- `client/update.sh`
- `client/update.ps1`

## Bootstrap Responsibilities

Bootstrap scripts should:

1. detect platform
2. resolve or require the target Unreal project root
3. download or locate the release bundle
4. verify the release bundle
5. materialize `Plugins/LoomleBridge/`
6. materialize `Loomle/`
7. print a clear success/failure summary

They should not download and execute a temporary installer binary.

## Artifact Contract

Bootstrap should consume release bundles and manifests directly.

It should not require a separate `loomle-installer` artifact.
It should not require Python or an internal bundle helper.

The relevant release assets are:

- `loomle-<platform>.zip`
- `loomle-manifest-<platform>.json`
- `loomle-manifest.json`

## Installed Outcome

Successful bootstrap should leave the project with:

- `Plugins/LoomleBridge/`
- `Loomle/loomle(.exe)`
- `Loomle/runtime/install.json`

## Relationship To Update

Update is not a public site entrypoint.

Installed projects keep:

- `Loomle/update.sh`
- `Loomle/update.ps1`
- `Loomle/doctor.sh`
- `Loomle/doctor.ps1`

## Decision

The first `0.4` bootstrap contract is:

- scripts only
- project-local install only
- no temporary installer binary
