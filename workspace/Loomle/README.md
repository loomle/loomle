# LOOMLE Workspace

This directory is the installed project-local control surface for LOOMLE.

Read this file first. It is the top-level usage guide for agents working inside this Unreal project.

## Core Rule

- Use `Loomle/loomle` as the only supported project-local LOOMLE entrypoint.
- Read this file for the main workflow.
- Open deeper files only when this file tells you why.

## Quick Start

Work in three phases:

### 1. Understand LOOMLE

1. Hello LOOMLE
   Read this workspace first so you understand the overall LOOMLE workflow, the available graph domains, the fallback policy, and the validation loop.
2. Link Check
   Run `Loomle/loomle doctor` first to confirm the project can see the plugin and MCP server. Run `Loomle/loomle list-tools` only when you need to confirm the live tool contract before assuming tool names or arguments.
3. Choose an execution mode
   - `Loomle/loomle call <tool-name> --args '<json-object>'`
     Use for one-shot tool execution.
   - `Loomle/loomle session`
     Use for repeated requests through one persistent stdin/stdout session. For high-concurrency or high-volume query workloads, prefer this mode because it avoids repeated client startup and is noticeably more efficient.

### 2. Understand This Project

4. Hello World
   Use `execute` to inspect the current Unreal project and discover what Python-side editor interfaces are actually available in this running session before assuming any specific library or plugin surface.
5. Context Routing
   Use `context` to understand the current editor focus and selection, then decide whether the task stays in world editing or should branch into `blueprint/`, `material/`, or `pcg/`.
6. If the task is graph editing, establish a stable graph address
   Use `graph.resolve` for any emitted object path before assuming a graph address.
   Working rule:
   - `context` tells you what the editor is focused on and what is selected
   - `graph.resolve` turns an asset path, object path, actor path, or component path into queryable `graphRef` values
   - once LOOMLE returns a `graphRef`, prefer reusing that `graphRef` in later `graph.query`, `graph.mutate`, and `graph.verify` calls

### 3. Edit And Validate

7. Pick the working surface
   - if the task is graph editing, branch into `blueprint/`, `material/`, or `pcg/` and read that domain's `GUIDE.md` first
   - if the task is world editing or editor automation, stay in `execute`
8. Make the edit through the primary surface
   - for graph work: use `graph.query` to read the current graph, then apply explicit primitive `graph.mutate` operations such as `addNode.byClass`, `connectPins`, and `setPinDefault`
   - for world work: use `execute` for targeted Unreal-side Python automation
   Working rule:
   - treat the workspace guides, semantics, catalogs, and examples as the primary semantic reference
   - treat `graph.mutate` as the stable graph execution layer
9. Escalate only when the primary surface is not enough
   - use `graph.*` first for supported graph capabilities
   - use `execute` when the task is non-graph editor automation, or when a graph-domain capability is not yet covered by `graph.*`
   - use `graph.mutate` with `op="runScript"` only for Blueprint graph-local gaps after you already have the exact target graph
   - use agent-local Python only for local file, repo, or payload preparation work; it does not replace Unreal-side `execute`
10. Validate the result
   - use `graph.query` for current graph readback truth
   - use `graph.verify` for final graph compile/refresh-backed confirmation
   - use `diag.tail` for recent Unreal-side diagnostics
   - use `editor.open`, `editor.focus`, and `editor.screenshot` when you need visual confirmation

## Reference Surfaces

Use the per-domain directories as the semantic reference surface.

- `blueprint/GUIDE.md`, `material/GUIDE.md`, `pcg/GUIDE.md`
  Start here when you branch into graph work.
- `SEMANTICS.md`
  Use when you need usage-level distinctions, not just task steps.
- `catalogs/`
  Use for static node facts and source-derived inventory.
- `examples/`
  Use for concrete payload shapes and edit patterns.

Working rule:
- start with `GUIDE.md`
- open `SEMANTICS.md` only when you need deeper usage guidance
- open catalogs and examples only when you need concrete lookup or payload detail

## Diagnostics

Use `diag.tail` when you want recent LoomleBridge diagnostics instead of only the current health snapshot.

Working rule:
- `fromSeq` is exclusive, so returned items satisfy `seq > fromSeq`
- `nextSeq` is the cursor to use on the next poll
- `hasMore=true` means more matching events are available after the returned page
- use `filters.severity`, `filters.category`, `filters.source`, or `filters.assetPathPrefix` to narrow noisy streams
- persisted diagnostic events live under `Loomle/runtime/diag/diag.jsonl`

## Visual Confirmation

When you need to see what the editor is showing:

- call `editor.open` with an `assetPath`
- call `editor.focus` with a semantic panel like `graph`, `viewport`, or `details`
- call `editor.screenshot`

Working rule:
- `editor.open` opens or focuses the asset editor for a Blueprint, Material, Material Function, PCG graph, or other asset-backed editor
- `editor.focus` focuses a semantic panel inside the active asset editor without exposing raw Unreal tab ids
- `editor.screenshot` writes a PNG of the active editor window to disk and returns the file path
- if you do not pass a path, screenshots go under `Loomle/runtime/captures/`

## Session Mode

`Loomle/loomle session` is the persistent mode.

Use it when:
- you want to send multiple requests without restarting the client
- you are building an integration
- you are load testing or benchmarking
- you expect high-concurrency or high-volume query traffic

Request shape:
- write one JSON request per line to stdin
- read one JSON response per line from stdout

Minimal example:

```json
{"id":1,"method":"tools/list"}
{"id":2,"method":"tools/call","params":{"name":"context","arguments":{}}}
```

## Install And Upgrade

- repair or reinstall: `loomle-installer install --project-root <ProjectRoot>`
- check for updates: `Loomle/loomle update`
- apply the latest update: `Loomle/loomle update --apply`
- apply a specific version: `Loomle/loomle update --version <Version>`

Working rule:
- project-local `loomle` delegates apply operations to a temporary installer
- after install or update writes new files, restart Unreal Editor if it is already running so the new LoomleBridge plugin version is loaded

## Official Skills

Use `loomle skill ...` when you need an official LOOMLE skill in your Codex skills directory.

Common commands:
- `Loomle/loomle skill list`
- `Loomle/loomle skill list --installed`
- `Loomle/loomle skill install material-weaver`
- `Loomle/loomle skill remove material-weaver`

Working rule:
- these commands manage official skills from the published `loomle/skills` registry
- they install into your local Codex skills directory, not into this Unreal project
- you can run them from inside or outside a project; they do not require `--project-root`

## Directory Map

```text
Loomle/
  README.md
  loomle(.exe)
  blueprint/
  material/
  pcg/
  runtime/
```

- `README.md`: the main agent-facing entrypoint
- `loomle(.exe)`: the installed project-local client entrypoint
- `blueprint/`, `material/`, `pcg/`: domain-specific guides, semantics, catalogs, and examples
- `runtime/`: machine-written state, not human guidance
