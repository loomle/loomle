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
- `context`: Read current project context.
- `selection`: Read current editor selection.
- `live`: Pull recent editor live events on demand.
- `execute`: Run Codex-generated UE Python actions.

## How It Works

You describe intent in natural language. Codex calls the matching MCP tool, `LoomleMcpBridge` executes or reads state inside UE Editor, and Codex returns concise human-readable results.

Transport is local IPC (socket / named pipe), not remote editor control.  
`execute` is an internal execution channel, so users usually describe intent instead of writing low-level parameters.  
By default, Loomle returns interpreted results instead of raw JSON (unless explicitly requested).

## Boundaries

- Keep Loomle content under `./Loomle`.
