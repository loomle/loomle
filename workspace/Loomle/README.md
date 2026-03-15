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
     Use for repeated requests through one persistent stdin/stdout session.
4. If you are starting from the current editor selection, use `context` first, then `graph.resolve` for any emitted object path before assuming a graph address.
   Working rule:
   - `context` tells you what the editor is focused on and what is selected
   - `graph.resolve` turns an asset path, object path, actor path, or component path into queryable `graphRef` values
   - once LOOMLE returns a `graphRef`, prefer reusing that `graphRef` in later `graph.query`, `graph.actions`, and `graph.mutate` calls
5. For Blueprint node creation, prefer the `graph.actions -> graph.mutate addNode.byAction` flow when you want editor-native addable actions instead of hardcoded class paths.
   Important:
   - `actionToken` is scoped to the exact graph that returned it.
   - Repeated `graph.actions` calls may return different tokens for equivalent actions.
   - If `addNode.byAction` reports an action-token error, refresh by calling `graph.actions` again on that same graph.
6. Choose the right workflow guide for the current graph type:
   - `workflows/blueprint.md`
   - `workflows/material.md`
   - `workflows/pcg.md`

## Commands

- `Loomle/loomle doctor`
  Check that LOOMLE is installed correctly in this project.
- `Loomle/loomle list-tools`
  Print the live tool contract exposed by the installed LOOMLE server.
- `Loomle/loomle call <tool-name> --args '<json-object>'`
  Make one tool request and print the result.
- `Loomle/loomle session`
  Start a persistent stdin/stdout JSON session for repeated requests.
- `Loomle/loomle update`
  Check the installed version against the latest published release.
- `Loomle/loomle update --apply`
  Upgrade this project-local LOOMLE install in place.

## Addressing Graphs

Preferred discovery order:
- use `context` when you want to start from the current editor state or selection
- use `graph.resolve` when you have a path from selection, an actor, a component, or an asset and need a queryable graph address
- use returned `graphRef` values as the stable addressing form for follow-up `graph.query`, `graph.actions`, and `graph.mutate` calls

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

## Action Tokens

Use `graph.actions` when you want LOOMLE to expose editor-native addable actions for the current Blueprint graph.

Working rule:
- call `graph.actions` on the exact target graph
- pick one `actions[*].actionToken`
- immediately use it in `graph.mutate` with `op="addNode.byAction"` on that same graph

Do not assume:
- that the same action will keep the same token across repeated `graph.actions` calls
- that a token from one asset, one root graph, or one inline subgraph will work on another graph

If mutate reports an action-token error, refresh with a new `graph.actions` call on the graph you are about to mutate.

## Mutate Batches

`graph.mutate` batches are ordered but not transactional.

Working rule:
- if the top-level result returns `applied=true`, the full batch succeeded
- if it returns `applied=false` and `partialApplied=true`, one or more earlier ops already changed the graph before a later op failed
- after any failed batch, run `graph.query` again before deciding what to retry

Do not assume a failed mutate batch automatically rolled back earlier successful ops.

## Install And Upgrade

Repair or reinstall from the project root:
- `loomle install --project-root <ProjectRoot>`

LOOMLE installs the plugin's prebuilt binaries and source together so Unreal can load quickly and still participate in local target rebuilds.

Check for an update:
- `Loomle/loomle update`

Apply the latest update:
- `Loomle/loomle update --apply`

Apply a specific version:
- `Loomle/loomle update --version <Version>`

## When To Open Deeper Files

- Open `workflows/blueprint.md` when the current task is editing or reading Blueprint graphs.
  This is also where `graph.actions` / `addNode.byAction` usage is explained.
- Open `workflows/material.md` when the current task is editing or reading Material graphs.
  This is where Material subgraph traversal through `childGraphRef` is explained.
- Open `workflows/pcg.md` when the current task is editing or reading PCG graphs.
  This is where graph resolution from selected PCG actors and components should be treated as normal.
- Open `examples/README.md` when you want small concrete payload examples before calling tools.
- Do not treat `runtime/` as documentation. It contains machine-written state such as install metadata.

## Directory Map

```text
Loomle/
  README.md
  loomle(.exe)
  workflows/
  examples/
  runtime/
```

- `README.md`: the main agent-facing entrypoint
- `loomle(.exe)`: the installed project-local client entrypoint
- `workflows/`: task-oriented operating guides by graph type
- `examples/`: small payload examples
- `runtime/`: machine-written state, not human guidance
