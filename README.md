# LOOMLE

LOOMLE brings Unreal Engine 5 runtime and graph context into AI-native workflows. It combines `LoomleBridge`, a standard MCP surface, and a project-local agent workspace so agents can collaborate with users directly inside real UE5 projects instead of working from partial context.

## Core Features

- Live UE5 context for AI: expose real-time editor, asset, selection, and runtime health data so agents act on facts, not snapshots.
- MCP-native integration: deliver LOOMLE through a clean, standard MCP surface that plugs directly into agent workflows.
- Graph-to-runtime collaboration: support graph inspection and mutation for Blueprint, Material, and PCG workflows with end-to-end project-local operation.

## Quick Start

Install LOOMLE directly into an Unreal project.

From the project root on macOS:

```bash
curl -fsSL https://loomle.ai/install.sh | bash -s -- --project-root "$PWD"
```

From the project root in Windows PowerShell:

```powershell
& ([scriptblock]::Create((irm https://loomle.ai/install.ps1))) --project-root (Get-Location).Path
```

These bootstrap commands install LOOMLE directly into one Unreal project from
the release manifest and platform zip. The `0.4` direction removes the
installer-binary handoff and keeps install/update as scripts rather than
`loomle` subcommands.

After install, the project-local entrypoint under `Loomle/` is:

```bash
Loomle/loomle
```

On Windows, use `Loomle\\loomle.exe`.

Installed projects keep update scripts under `Loomle/`:

- macOS/Linux: `Loomle/update.sh`
- Windows: `Loomle/update.ps1`

LOOMLE is now organized around a single repository and a single installed project shape:

- native MCP runtime shipped with `Plugins/LoomleBridge`
- project-local client and agent workspace shipped under `Loomle/`

For a source checkout, the current path is:

1. Build a local release:

```bash
python3 packaging/release/build_local_release.py \
  --repo-root /path/to/loomle \
  --output-dir /tmp/loomle-release \
  --platform darwin \
  --version 0.0.0-dev
```

2. Or build and install directly into a UE project:

```bash
python3 packaging/install/install_from_checkout.py \
  --repo-root /path/to/loomle \
  --project-root /path/to/MyProject \
  --output-dir /tmp/loomle-install-build \
  --platform darwin \
  --version 0.0.0-dev
```

For day-to-day source-checkout validation, prefer the unified developer flow:

```bash
python3 tools/dev_verify.py \
  --project-root /path/to/MyProject
```

That flow installs the current checkout into the target project, restarts Unreal Editor, and validates through the same project-local `Loomle/loomle` entrypoint that users run after installation.

See [`docs/REPO_STRUCTURE.md`](docs/REPO_STRUCTURE.md) for the target repository, release, and installed-project structure.
See [`packaging/BOOTSTRAP_CONTRACT.md`](packaging/BOOTSTRAP_CONTRACT.md) for the public bootstrap entrypoint contract.

Once setup is complete, you do not need to call MCP tools yourself. You can simply talk to Codex in natural language and ask it to do UE5 work for you through LOOMLE.

Example requests:

- `Look at the current Blueprint, explain why this logic is failing, and fix it.`
- `Inspect the selected material graph and clean up unnecessary nodes and connections.`
- `Create a Blueprint that opens a door when the player enters a trigger box.`

## Agent-Facing MCP Tools

LOOMLE currently exposes these MCP tools for the agent. These are the tools Codex uses behind the scenes; end users normally just work through natural-language requests.

For graph work, the recommended entrypoint is the installed workspace under [`workspace/Loomle/README.md`](workspace/Loomle/README.md). Agents should start there, then use graph-specific `GUIDE.md`, `SEMANTICS.md`, catalogs, and examples before planning edits.

- `loomle`: Check Bridge health, runtime status, and MCP-side availability.
- `context`: Read the current Unreal editor context, including asset and selection information.
- `execute`: Run UE-side code or commands through the Bridge. Supports synchronous execution by default and long-running submission through `execution.mode = "job"`.
- `jobs`: Inspect long-running job state, logs, results, and outstanding work for job-mode submissions.
- `profiling`: Read official Unreal profiling data families such as `stat unit`, `stat game`, `stat gpu`, `dumpticks`, and memory summary bridges.
- `editor.open`: Open or focus the editor for a specific Unreal asset.
- `editor.focus`: Focus a semantic panel inside an asset editor.
- `editor.screenshot`: Capture the active editor window for visual confirmation.
- `graph`: Read graph capability metadata, supported operations, and runtime status.
- `graph.list`: List graphs available in the current target asset or context.
- `graph.resolve`: Resolve an asset, object, actor, or component path into reusable `graphRef` values.
- `graph.query`: Inspect graph structure such as nodes, pins, and connections.
- `graph.mutate`: Apply graph changes through ordered mutation operations.
- `graph.verify`: Run compile-backed graph verification after a read or mutate loop.
- `diag.tail`: Read persisted LoomleBridge diagnostics incrementally.

`graph.query` is now the primary graph read contract. Shared graph-native readback
centers on:

- pin-default truth
- grouped `effectiveSettings`
- `childGraphRef`

Blueprint also promotes richer product-native surfaces where needed, including:

- `embeddedTemplate`
- `graphBoundarySummary`
- `contextSensitiveConstruct`

For long-running Unreal work, prefer `execute` submission with
`execution.mode = "job"` and then use top-level `jobs` to inspect lifecycle,
logs, and final results.

When Unreal is in `PIE`, `execute` and `jobs` remain available. Treat `PIE` as
runtime context, not as a blanket execute lockout. Structured `graph.*` and
editor-facing tools may still stay blocked during `PIE` depending on their
contract.

For protocol details and deeper technical documentation, see [`docs/README.md`](docs/README.md).
