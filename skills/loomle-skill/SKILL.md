---
name: loomle-skill
description: Install, upgrade, configure, verify, and operate Loomle in Unreal projects using a project-local workflow. Use when users ask to install/update Loomle, fix Loomle runtime setup, run Loomle bridge checks, or execute Loomle MCP graph/context workflows such as Blueprint or material graph tasks.
---

# Loomle Skill

Use this skill as the single operational playbook for Loomle inside a project.

## Enforce Single Source Of Truth

- Read `/Users/xartest/Documents/UnrealProjects/Loomle/Loomle/AGENTS.md` before any Loomle action.
- Keep Loomle content under `./Loomle`.
- Use one install mode by default: project-local install only.
- Do not use UE global plugin install unless the user explicitly asks for global mode.

## Trigger Phrases

Use this skill when requests match intents like:
- `install Loomle`
- `upgrade Loomle`
- `fix Loomle setup`
- `check Loomle bridge`
- `use Loomle to edit Blueprint/material graph`

## Workflow Decision

1. Install/Upgrade flow: user asks for install, update, or setup repair.
2. Runtime usage flow: user asks for Loomle MCP operations (`loomle`, `context`, `execute`, `graph.*`).

## Skill Version Check

- Check latest published skill version from:
  `https://github.com/loomle/loomle/releases/download/skill-latest/loomle-skill-version.json`
- Use this endpoint when user asks whether local `loomle-skill` is up to date.

## Install/Upgrade Flow (Project-Local)

1. Resolve project root from current workspace (`*.uproject` at root expected).
2. Resolve target plugin path as `<ProjectRoot>/Loomle/Plugins/LoomleBridge`.
3. Use release artifact mode:
- Read a remote release manifest.
- Use stable manifest URL:
  `https://github.com/loomle/loomle/releases/download/bridge-latest/loomle-bridge-manifest.json`
- Pick platform package (`windows`, `darwin`, `linux`).
- Download package into local cache.
- Verify `sha256` before unpack/install.
- Replace only the target plugin directory.
4. Apply required editor performance setting in `<ProjectRoot>/Config/DefaultEditorSettings.ini`:
- `[/Script/UnrealEd.EditorPerformanceSettings]`
- `bThrottleCPUWhenNotForeground=False`
5. Run post-install verification using Loomle scripts/tools.
6. Return concise result: installed version, plugin path, verify status, and next action if failed.

### Temporary Compatibility Note

If release manifest is not available yet, use existing local entrypoints strictly as a bridge path:
- install: `./Loomle/scripts/install_loomle_bridge.sh`
- upgrade: `./Loomle/scripts/upgrade_loomle_bridge.sh`
Remove this fallback once release-based installer is fully live.

## Runtime Usage Flow

1. Route by intent:
- Health/status -> `loomle`
- Active editor context -> `context`
- Custom read/write -> `execute`
- Graph schema -> `graph`
- Graph list/read/write -> `graph.list` / `graph.query` / `graph.mutate`
2. Return natural-language output by default.
3. Do not dump raw JSON unless explicitly requested.
4. When operations fail, append actionable recovery steps (restart editor, verify socket/pipe, rerun checks).

## MCP Technique Expansion (v2+)

Extend this skill with focused guides under `references/`:
- material graph workflows
- blueprint graph refactoring patterns
- deterministic node layout and wiring conventions
- troubleshooting playbooks by error signature

Load only the needed reference file for the current request.

## Bundled Resources

- Script for release-based project install:
  `scripts/project_install_release.sh`
- Script for post-install checks:
  `scripts/project_verify_loomle.sh`
- Manifest schema and release layout:
  `references/release-manifest.md`
