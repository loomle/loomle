# Loomle

## What It Is

Loomle is an AI-native toolkit for building AAA Unreal projects with natural language.  
It combines editor control, automation, skills, and extensible modules.

## Quick Start

For Human (paste this in Codex from your Unreal project root):

```text
Install Loomle (https://github.com/loomle/loomle) by following ./Loomle/README.md.
```

For Agent (single install entrypoint):

```bash
./Loomle/scripts/install_loomle.sh
```

## What It Can Do

- `loomle`: Show bridge health and capability summary.
- `context`: Read current project context and selection snapshot.
- `live`: Pull recent editor live events on demand.
- `graph`: Return graph capability/schema descriptor.
- `graph.list`: List readable graphs in a graph asset (`blueprint`, `material`/`shader`, `pcg`).
- `graph.query`: Read semantic graph snapshot (`nodes`, `edges`, `signature`) with graph-type specific diagnostics.
- `graph.addable`: List addable actions for graph/pin context (`blueprint`, `material`/`shader`, `pcg`).
- `graph.mutate`: Apply graph write operations (`blueprint`, `material`/`shader`, `pcg`; op coverage varies by graph type).
- `graph.watch`: Pull graph-scope event stream with cursor.
- `execute`: Run Codex-generated UE Python actions.

## How It Works

You describe intent in natural language. Codex calls the matching bridge tool, `LoomleBridge` executes or reads state inside UE Editor, and Codex returns concise human-readable results.

Transport is local IPC (socket / named pipe), not remote editor control.  
`execute` is an internal execution channel, so users usually describe intent instead of writing low-level parameters.  
By default, Loomle returns interpreted results instead of raw JSON (unless explicitly requested).

## Boundaries

- Keep Loomle content under `./Loomle`.
