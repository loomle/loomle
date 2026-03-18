# LOOMLE Workspace

This directory is the installed project-local control surface for LOOMLE.

Read this file first. It is the top-level usage guide for agents working inside this Unreal project.

## Core Rule

- Use `Loomle/loomle` as the only supported project-local LOOMLE entrypoint.
- Read this file for the main workflow.
- Open deeper files only when this file tells you why.

## Quick Start

1. Run `Loomle/loomle doctor`
   Use this first to confirm the project can see the plugin and MCP server.
2. Run `Loomle/loomle list-tools`
   Use this to discover the live tool contract from the installed server before assuming tool names or arguments.
3. Choose the right execution mode:
   - `Loomle/loomle call <tool-name> --args '<json-object>'`
     Use for one-shot tool execution.
   - `Loomle/loomle session`
     Use for repeated requests through one persistent stdin/stdout session. For high-concurrency or high-volume query workloads, prefer this mode because it avoids repeated client startup and is noticeably more efficient.
4. If you are starting from the current editor selection, use `context` first, then `graph.resolve` for any emitted object path before assuming a graph address.
   Working rule:
   - `context` tells you what the editor is focused on and what is selected
   - `graph.resolve` turns an asset path, object path, actor path, or component path into queryable `graphRef` values
   - once LOOMLE returns a `graphRef`, prefer reusing that `graphRef` in later `graph.query`, `graph.mutate`, and `graph.verify` calls
5. For graph editing, prefer the semantic-reference flow:
   - read the workspace reference files first for the current graph type
   - use `graph.query` to read the current graph shape, pins, settings, and diagnostics
   - apply explicit primitive `graph.mutate` operations such as `addNode.byClass`, `connectPins`, and `setPinDefault`
   - close the loop with `graph.query` and `graph.verify`
   Working rule:
   - treat the workspace catalogs and guides as the primary semantic reference
   - treat `graph.mutate` as the stable execution layer
   - use `graph.query` when you want the current lightweight graph diagnostics and readback truth
   - use `graph.verify` when you want final compile-backed verification
6. Follow the fallback policy when the structured graph surface is not enough:
   - use `graph.*` first for supported graph types and already-covered graph capabilities
   - use `execute` when the task is non-graph editor automation, or when the graph type or graph-domain capability is not yet covered by `graph.*`
   - use `graph.mutate` with `op="runScript"` only for Blueprint graph-local gaps after you already have the exact target graph
   - use agent-local Python only for local file, repo, or payload preparation work; it does not replace Unreal-side `execute`
   Working rule:
   - `execute` means running Python inside the Unreal Editor process
   - `runScript` is Blueprint-only today; it is not a general fallback for Material, PCG, or unsupported graph types
7. Choose the right workflow guide for the current graph type:
   - `blueprint/GUIDE.md`
   - `material/GUIDE.md`
   - `pcg/GUIDE.md`
8. When you need recent Unreal-side warnings, compile failures, or runtime diagnostics, call `diag.tail`.
   Working rule:
   - use `fromSeq` as an exclusive cursor
   - reuse returned `nextSeq` on the next poll
   - use `filters` when you want to narrow by severity, category, source, or asset path prefix
9. When you need a verification step after reading or mutating a graph, call `graph.verify`.
   Working rule:
   - use `graph.query` for current lightweight diagnostics from the semantic snapshot
   - use `graph.verify` for final compile/refresh-backed confirmation
   - `graph.verify` is graph-scoped; do not use it for scene/runtime instance debugging

## Visual Loop

When you need the editor to show a specific asset and then capture what it looks like:

- call `editor.open` with an `assetPath`
- then call `editor.focus` with a semantic panel like `graph`, `viewport`, or `details`
- then call `editor.screenshot`

Working rule:
- `editor.open` opens or focuses the asset editor for a Blueprint, Material, Material Function, PCG graph, or other asset-backed editor
- `editor.focus` focuses a semantic panel inside the active asset editor without exposing raw Unreal tab ids
- `editor.screenshot` writes a PNG of the active editor window to disk and returns the file path
- if you do not pass a path, screenshots go under `Loomle/runtime/captures/`

## Commands

- `Loomle/loomle doctor`
  Check that LOOMLE is installed correctly in this project.
- `Loomle/loomle list-tools`
  Print the live tool contract exposed by the installed LOOMLE server.
- `Loomle/loomle call <tool-name> --args '<json-object>'`
  Make one tool request and print the result.
- `Loomle/loomle call diag.tail --args '{"fromSeq":0}'`
  Read persisted diagnostics incrementally. Reuse the returned `nextSeq` as the next cursor.
- `Loomle/loomle call graph.verify --args '{"graphType":"pcg","assetPath":"/Game/PCG/MyGraph"}'`
  Run an explicit compile/refresh verification for one graph asset.
- `Loomle/loomle session`
  Start a persistent stdin/stdout JSON session for repeated requests. Prefer this for high-concurrency or high-volume query workloads.
- `Loomle/loomle skill list`
  List official LOOMLE skills from the published registry. Add `--installed` to show only already-installed official skills.
- `Loomle/loomle skill install <skill-name>`
  Install one official LOOMLE skill into the local Codex skills directory.
- `Loomle/loomle skill remove <skill-name>`
  Remove one previously installed official LOOMLE skill from the local Codex skills directory.
- `Loomle/loomle update`
  Check the installed version against the latest published release.
- `Loomle/loomle update --apply`
  This downloads a temporary `loomle-installer`, hands off the upgrade, and exits.
  Upgrade this project-local LOOMLE install in place.
  After install or update writes new files, restart Unreal Editor if it is already running so the new LoomleBridge plugin version is loaded.

## Addressing Graphs

Preferred discovery order:
- use `context` when you want to start from the current editor state or selection
- use `graph.resolve` when you have a path from selection, an actor, a component, or an asset and need a queryable graph address
- use returned `graphRef` values as the stable addressing form for follow-up `graph.query`, `graph.mutate`, and `graph.verify` calls

Do not guess:
- that a selected object path is already the right `assetPath`
- that Blueprint, Material, and PCG all resolve the same way from editor selection

If LOOMLE gives you a `graphRef`, prefer passing that back verbatim instead of reconstructing `assetPath` / `graphName` yourself.

## Session Mode

`loomle session` is the persistent mode.

Use it when:
- you want to send multiple requests without restarting the client
- you are building an integration
- you are load testing or benchmarking
- you expect high-concurrency or high-volume query traffic and want the more efficient transport path

Request shape:
- write one JSON request per line to stdin
- read one JSON response per line from stdout

Minimal examples:

```json
{"id":1,"method":"tools/list"}
{"id":2,"method":"tools/call","params":{"name":"context","arguments":{}}}
```

Responses:
- successful responses include `"ok": true` and `result`
- failed responses include `"ok": false` and `error`
- each response includes the same `id` as the request

## Diagnostics

Use `diag.tail` when you want recent LoomleBridge diagnostics instead of only the current health snapshot.

Minimal one-shot example:

```bash
Loomle/loomle call diag.tail --args '{"fromSeq":0,"limit":50}'
```

Working rule:
- `fromSeq` is exclusive, so returned items satisfy `seq > fromSeq`
- `nextSeq` is the cursor to use on the next poll
- `hasMore=true` means more matching events are available after the returned page
- use `filters.severity`, `filters.category`, `filters.source`, or `filters.assetPathPrefix` to narrow noisy streams
- persisted diagnostic events live under `Loomle/runtime/diag/diag.jsonl`

## Graph Verification

Use `graph.verify` as the final verification primitive in the graph loop.

Minimal one-shot example:

```bash
Loomle/loomle call graph.verify --args '{"graphType":"pcg","assetPath":"/Game/PCG/MyGraph"}'
```

Working rule:
- use `graph.query` when you want structural diagnostics from the latest graph snapshot
- use `graph.verify` when you want final compile/refresh-backed confirmation
- `graph.verify` is for graph assets only; runtime scene debugging is a separate concern

## Semantic References

Use the per-graph directories as the default semantic reference surface.

Working rule:
- start with the graph-specific directory before reaching for external skills
- use `GUIDE.md` for the shortest working path
- use `SEMANTICS.md` for usage-level semantic distinctions
- use catalogs for node discovery and static node facts
- use examples for concrete mutate payload shapes

Current expectation:
- workspace references are static and agent-readable
- execution still happens through primitive `graph.mutate` operations
- `graph.query` and `graph.verify` are the validation loop after any meaningful edit

## Reference Layers

Each graph-specific directory is organized into reference layers:

- `GUIDE.md`
  Use for the shortest working path:
  read the graph, mutate with primitive ops, then validate.
- `SEMANTICS.md`
  Use when you need usage-level help:
  which node family fits, which parameters carry meaning, which pins matter,
  and which nearby nodes are easy to confuse.
- `catalogs/`
  Use for static node facts:
  class names, properties, dynamic-pin hints, and other inventory-style data.
- `examples/`
  Use for concrete payload patterns:
  small mutate batches that show how to express a real edit.

Working rule:
- `GUIDE.md` should stay short
- `SEMANTICS.md` should explain usage, not just concepts
- catalogs and examples should provide the concrete lookup and execution detail

## Mutate Batches

`graph.mutate` batches are ordered but not transactional.

Working rule:
- if the top-level result returns `applied=true`, the full batch succeeded
- if it returns `applied=false` and `partialApplied=true`, one or more earlier ops already changed the graph before a later op failed
- after any failed batch, run `graph.query` again before deciding what to retry
- reserve `op="runScript"` for Blueprint graph-scoped fallback logic after you already have the exact `graphRef`
- if the task is non-graph, targets Material or PCG, or the graph type/capability is not yet covered by `graph.*`, use `execute` instead of `runScript`

Do not assume a failed mutate batch automatically rolled back earlier successful ops.

## Install And Upgrade

Repair or reinstall from the project root:
- `loomle-installer install --project-root <ProjectRoot>`
  If Unreal Editor is already running, restart it after install so the newly installed LoomleBridge plugin version is loaded.

LOOMLE installs the plugin's prebuilt binaries and source together so Unreal can load quickly and still participate in local target rebuilds.

Check for an update:
- `Loomle/loomle update`

Apply the latest update:
- `Loomle/loomle update --apply`
  Project-local `loomle` does not self-upgrade directly; it delegates apply operations to a temporary installer.
  If Unreal Editor is already running, restart it after the update so the editor loads the new LoomleBridge plugin version.

Apply a specific version:
- `Loomle/loomle update --version <Version>`

## Official Skills

Use `loomle skill ...` when you need an official LOOMLE skill in your Codex skills directory.

List official skills:
- `Loomle/loomle skill list`

Show only already-installed official skills:
- `Loomle/loomle skill list --installed`

Install one skill:
- `Loomle/loomle skill install material-weaver`

Remove one skill:
- `Loomle/loomle skill remove material-weaver`

Working rule:
- these commands manage official skills from the published `loomle/skills` registry
- they install into your local Codex skills directory, not into this Unreal project
- you can run them from inside or outside a project; they do not require `--project-root`

## When To Open Deeper Files

- Open `blueprint/GUIDE.md` when the current task is editing or reading Blueprint graphs.
- Open `blueprint/SEMANTICS.md` when the current task needs Blueprint node-family or control-flow semantics.
- Open `material/GUIDE.md` when the current task is editing or reading Material graphs.
- Open `material/SEMANTICS.md` when the current task needs root-sink or Material function semantics.
- Open `pcg/GUIDE.md` when the current task is editing or reading PCG graphs.
- Open `pcg/SEMANTICS.md` when the current task needs PCG node-family, parameter, or wiring semantics.
- Open `pcg/catalogs/node-catalog.json` when you need the static UE node inventory for PCG.
- Do not treat `runtime/` as documentation. It contains machine-written state such as install metadata.

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
- `blueprint/`: Blueprint guide, semantics, and examples
- `material/`: Material guide, semantics, and examples
- `pcg/`: PCG guide, semantics, examples, and catalogs
- `runtime/`: machine-written state, not human guidance
