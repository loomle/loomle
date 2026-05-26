# LOOMLE

Agent-native Unreal Engine MCP server for Blueprint, Material, PCG, and Widget
workflows.

LOOMLE helps Claude Code, Codex, and other MCP-compatible coding agents operate
live Unreal Engine projects through tools that match UE semantics. It does not
ask agents to guess internal node classes, invent graph transformations, or
treat UE assets as generic JSON.

Use LOOMLE when you want an AI agent to inspect or edit a real UE project,
including Blueprint graphs, Material graphs, PCG graphs, UMG WidgetBlueprints,
asset metadata, editor context, compile diagnostics, and play sessions.

## What It Provides

- A global `loomle mcp` command for Claude Code, Codex, and other MCP hosts.
- A UE editor bridge installed into each project as `Plugins/LoomleBridge`.
- UE-semantic tools for project/session setup, assets, Blueprint, Material,
  PCG, UMG widgets, editor focus, runtime execution, diagnostics, logs, and
  play sessions.
- Palette-driven creation so agents use UE's own creation model instead of
  guessing classes.
- Compact first-level schemas with `schema.inspect` for detailed operation
  schemas when a tool intentionally has a second layer.

## Why Not Just Python?

Unreal Python is useful for basic editor automation, but many valuable editor
workflows sit behind UE's own C++ editor APIs, graph schemas, palette actions,
K2 nodes, pin reconstruction, compiler behavior, and asset-specific editors.
LOOMLE exposes those workflows as explicit agent tools instead of treating the
editor as a generic script box.

The goal is not prompt-to-game magic. The goal is a reliable UE-native control
surface for agents working inside existing projects.

## Install

macOS and Linux:

```bash
curl -fsSL https://loomle.ai/install.sh | bash
```

Windows PowerShell:

```powershell
& ([scriptblock]::Create((irm https://loomle.ai/install.ps1)))
```

The installer creates a global install under `~/.loomle` by default and exposes
a stable command at `~/.loomle/bin/loomle`. MCP hosts should run:

```bash
loomle mcp
```

See the full install guide: https://loomle.ai/install.html

## Project Model

LOOMLE has two installation scopes:

- Global install: the `loomle` command, active version state, release payloads,
  plugin cache, and runtime/project registry under `~/.loomle`.
- Project install: the Unreal project plugin under `Plugins/LoomleBridge` plus
  required project support settings.

The global install is not tied to one Unreal project. When Unreal Editor starts
with `LoomleBridge` loaded, the project reports a runtime endpoint. The current
MCP session then uses `project.list` and `project.attach` to select one online
project.

If a project does not have LOOMLE support yet, close Unreal Editor for that
project and run the MCP tool `project.install` with the target `projectRoot`.

## Quickstart

From an MCP host session:

1. Call `project.list` to find online projects.
2. Call `project.attach` with the target `projectId` or `projectRoot`.
3. Call `context` if the user already has an asset open or selected in UE.
4. Inspect before editing.
5. Use palettes for creation.
6. Compile or verify after meaningful asset changes.

Full quickstart: https://loomle.ai/quickstart.html

## Trying LOOMLE?

The fastest useful test is:

1. Install LOOMLE and configure your MCP host.
2. Open an Unreal project.
3. Run `project.install` if the project does not have `Plugins/LoomleBridge`.
4. Restart Unreal Editor, then call `project.list`, `project.attach`, and
   `context`.
5. Try one domain inspect tool, such as `blueprint.graph.inspect`,
   `material.graph.inspect`, `pcg.graph.inspect`, or `widget.tree.inspect`.

If anything is confusing or fails, open a
[first user feedback issue](https://github.com/loomle/loomle/issues/new?template=first-user-feedback.yml).
Early feedback is especially useful when it includes the MCP host, Unreal
version, the first unclear step, and the tool output.

## Tool Surface

The public MCP tools are grouped by UE domain:

- Project and session: `loomle.status`, `project.list`, `project.attach`,
  `project.install`, `schema.inspect`, `loomle`, `context`.
- Assets: `asset.create`, `asset.inspect`, `asset.edit`.
- Blueprint: class contract, members, graphs, node-local edits, palette, and
  compile tools.
- Material: graph, node, palette, layout, and compile tools.
- PCG: graph, palette, node settings, graph parameters, layout, and compile
  tools.
- Widget: UMG palette, WidgetTree, widget inspect, and compile tools.
- Runtime/editor/diagnostics: `execute`, `jobs`, `profiling`, `play`,
  `editor.*`, `diagnostic.tail`, and `log.tail`.

API reference: https://loomle.ai/tools/

## License

LOOMLE is open source under the MIT License. See [LICENSE](LICENSE).

## Search Keywords

LOOMLE is an Unreal Engine MCP server for AI agents, Claude Code, Codex,
Blueprint MCP workflows, PCG MCP workflows, UMG / WidgetBlueprint automation,
Material graph tooling, UE editor automation, and local-first Unreal agent
tooling.

## CLI

The CLI stays intentionally small:

- `loomle mcp`: run the stdio MCP server.
- `loomle update`: update the global LOOMLE install.
- `loomle doctor`: inspect the global install and print MCP configuration hints.

MCP tools are not duplicated as CLI subcommands.

## Development

Build and validate the local checkout against a UE project:

```bash
python3 tools/dev_verify.py --project-root /path/to/MyProject
```

That flow builds the Rust client, syncs `engine/LoomleBridge` into the dev
project, starts Unreal Editor, and validates through the checkout-built
`loomle` binary.

Release bundles contain only:

- `loomle` / `loomle.exe`
- `plugin-cache/LoomleBridge/`

They do not include archived workspace reference material or a per-project
client.

## Documentation

- Website: https://loomle.ai/
- Install: https://loomle.ai/install.html
- Quickstart: https://loomle.ai/quickstart.html
- Tools API: https://loomle.ai/tools/
- Releases: https://github.com/loomle/loomle/releases

Current design documents live under `docs/`. Historical notes and obsolete
specs are under `docs/archive/legacy/` and are not the current public contract.
