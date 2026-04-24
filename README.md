# LOOMLE

LOOMLE connects AI coding agents to live Unreal Engine projects through MCP.
It has two pieces:

- a global `loomle` command that runs as the MCP server process for Codex,
  Claude, or another MCP host
- `LoomleBridge`, a UE plugin installed into each project that reports the
  running editor and executes UE-side tools

There is no per-project client workspace in the current model.
Projects only receive `Plugins/LoomleBridge/`.

## Install

Install LOOMLE globally:

```bash
curl -fsSL https://loomle.ai/install.sh | bash
```

Windows PowerShell:

```powershell
& ([scriptblock]::Create((irm https://loomle.ai/install.ps1)))
```

The installer writes the global install under `~/.loomle` by default and
creates a stable `loomle` command under `~/.loomle/bin/`.

Configure your MCP host to run:

```bash
loomle mcp
```

LOOMLE does not use a daemon. Each MCP host session starts its own stdio
`loomle mcp` process and attaches to one active Unreal project in that
process.

## Project Setup

Open the target Unreal project with `LoomleBridge` installed. The bridge writes
an online runtime record under `~/.loomle/state/runtimes/`.

From an MCP session:

1. Call `project.list` to see active projects.
2. Call `project.attach` with a listed project.
3. Use the UE tools exposed by the attached bridge.

To install or update project support:

```text
project.install
```

`project.install` copies the cached global plugin into
`<ProjectRoot>/Plugins/LoomleBridge/`. It is both install and upgrade. Close the
Unreal Editor for that project before running it.

## CLI

The command intentionally stays small:

- `loomle mcp`: run the stdio MCP server
- `loomle update`: update the global LOOMLE install
- `loomle doctor`: inspect the global install and print MCP configuration hints

MCP tools are not duplicated as CLI subcommands.

## Developer Flow

Build and validate the local checkout:

```bash
python3 tools/dev_verify.py --project-root /path/to/MyProject
```

That flow builds the Rust client, syncs `engine/LoomleBridge` into the dev
project, starts Unreal Editor, and validates through the checkout-built
`loomle` binary.

Release bundles contain only:

- `loomle` / `loomle.exe`
- `plugin-cache/LoomleBridge/`

They do not include the repository-internal `docs/archive/workspace/Loomle` reference
material or any per-project client.

## Docs

- [Global install model](docs/LOOMLE_GLOBAL_INSTALL_MODEL.md)
- [Repository structure](docs/REPO_STRUCTURE.md)
- [Packaging contract](packaging/install/INSTALL_CONTRACT.md)
- [MCP protocol](docs/MCP_PROTOCOL.md)
- [Graph domain split spec](docs/spec-graph-domain-split.md)
