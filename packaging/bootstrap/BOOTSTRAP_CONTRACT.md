# LOOMLE Bootstrap Contract

## Goal

Support a machine with no existing LOOMLE setup.

The user should be able to start from a single public entrypoint, download a temporary `loomle-installer`, execute one install or update operation, and then discard it.

## Public entrypoint

The canonical public installer entrypoint should be:

- `https://loomle.ai/i`

Recommended behavior:

- browser requests to `https://loomle.ai/i` should land on a small install page that offers both shell and PowerShell commands
- command-line bootstrap scripts should live at stable URLs:
  - `https://loomle.ai/install.sh`
  - `https://loomle.ai/install.ps1`

## Bootstrap responsibilities

Bootstrap scripts should:

1. detect the current platform
2. download the temporary `loomle-installer` binary for that platform
3. execute it with the requested install or update arguments
4. delete the temporary binary after the installer exits

Bootstrap scripts should not install a global CLI and should not leave a machine-level `loomle` binary behind.

## Artifact contract

Bootstrap should download a standalone installer artifact, not the project-local `Loomle/loomle`.

Current hosting model:

- stable public entrypoints remain `https://loomle.ai/install.sh` and `https://loomle.ai/install.ps1`
- those scripts fetch versioned binaries from GitHub Releases
- the stable alias release is `loomle-latest`
- current published bootstrap binaries are macOS and Windows; Linux should fail fast until a Linux release job exists

Recommended release asset shape on the stable alias release:

```text
loomle-latest/
  loomle-installer
  loomle-installer.exe
  loomle-manifest.json
  loomle-darwin.zip
  loomle-windows.zip
```

The temporary installer may share code with `mcp/client`, but it is a distinct role:

- `loomle-installer`:
  - bootstrap target
  - executes install, update, and repair flows
  - is downloaded temporarily and removed after use
- project-local `loomle`:
  - installed into `Loomle/loomle`
  - provides project runtime entrypoint
  - may hand off `update --apply` to a temporary installer

## Redirect target

If `https://loomle.ai/i` must redirect to a concrete script endpoint, the preferred target is:

- `https://loomle.ai/install.sh`

and the install page at `https://loomle.ai/i` should also visibly show the Windows alternative:

- `powershell -ExecutionPolicy Bypass -Command "irm https://loomle.ai/install.ps1 | iex"`
