# Loomle

## What It Is

Loomle is an AI-native toolkit for building AAA Unreal projects with natural language.  
It combines editor control, automation, skills, and extensible modules.

## Quick Start

Step 1: Ask Codex to install `loomle-skill` from release zip URL.

```text
Install loomle-skill from https://github.com/loomle/loomle/releases/latest/download/loomle-skill.zip (overwrite if exists).
```

Step 2: Ask Codex to install or upgrade Loomle Bridge for the current Unreal project.

```text
Install Loomle Bridge for this Unreal project.
```

or

```text
Upgrade Loomle Bridge for this Unreal project.
```

## What It Can Do

- `loomle`: Show bridge health and capability summary.
- `context`: Read current project context and selection snapshot.
- `graph`: Return graph capability/schema descriptor.
- `graph.list`: List readable graphs in a graph asset (`blueprint`, `material`/`shader`, `pcg`).
- `graph.query`: Read semantic graph snapshot (`nodes`, `edges`, `signature`) with graph-type specific diagnostics.
- `graph.actions`: List addable actions for graph/pin context (`blueprint`, `material`/`shader`, `pcg`).
- `graph.mutate`: Apply graph write operations (`blueprint`, `material`/`shader`, `pcg`; op coverage varies by graph type).
- `execute`: Run Codex-generated UE Python actions.

## How It Works

You describe intent in natural language. Codex calls the matching bridge tool, `LoomleBridge` executes or reads state inside UE Editor, and Codex returns concise human-readable results.

Transport is local IPC (socket / named pipe), not remote editor control.  
`execute` is an internal execution channel, so users usually describe intent instead of writing low-level parameters.  
By default, Loomle returns interpreted results instead of raw JSON (unless explicitly requested).

## Boundaries

- Keep Loomle content under `./Loomle`.

## Release Triggers

- Plugin release (Mac): push tag `vX.Y.Z` -> workflow `release-loomle-bridge-mac.yml`
- Skill release: push tag `skill-vA.B.C` -> workflow `release-loomle-skill.yml`
