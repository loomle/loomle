# LOOMLE Bootstrap Contract

## Goal

Support a machine with no existing LOOMLE setup.

The user should be able to start from a single public entrypoint and end up with a usable `loomle` command on their machine.

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
2. download the global `loomle` CLI binary
3. place it in a standard user-local bin directory
4. print the next-step command:
   - `loomle install --project-root <ProjectRoot>`

Bootstrap scripts should not install directly into a UE project.
That work belongs to the global CLI after bootstrap.

## Global CLI placement

Recommended install destinations:

### macOS / Linux

```text
$HOME/.local/bin/loomle
```

### Windows

```text
%LOCALAPPDATA%\\Programs\\Loomle\\bin\\loomle.exe
```

## Artifact contract

Bootstrap should download a standalone global CLI artifact, not the project-local `Loomle/loomle`.

Current hosting model:

- stable public entrypoints remain `https://loomle.ai/install.sh` and `https://loomle.ai/install.ps1`
- those scripts fetch versioned binaries from GitHub Releases
- the stable alias release is `loomle-latest`
- current published bootstrap binaries are macOS and Windows; Linux should fail fast until a Linux release job exists

Recommended release asset shape on the stable alias release:

```text
loomle-latest/
  loomle-darwin
  loomle-linux
  loomle.exe
  loomle-manifest.json
  loomle-darwin.zip
  loomle-windows.zip
```

This global CLI may share code with `mcp/client`, but it is a distinct installed role:

- global CLI:
  - bootstrap target
  - provides `loomle install`
  - machine-level entrypoint
- project-local client:
  - installed into `Loomle/loomle`
  - provides project runtime entrypoint

## Redirect target

If `https://loomle.ai/i` must redirect to a concrete script endpoint, the preferred target is:

- `https://loomle.ai/install.sh`

and the install page at `https://loomle.ai/i` should also visibly show the Windows alternative:

- `powershell -ExecutionPolicy Bypass -Command "irm https://loomle.ai/install.ps1 | iex"`
