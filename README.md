# Loomle

Loomle is an agent-native Unreal Engine integration for reading and changing
complex editor objects through SAL, the Structured Agent Language.

SAL turns Blueprint graphs, Widget trees, Class reflection, Assets, and other
non-text UE state into compact, ordered text that both people and agents can
read, copy, discuss, and patch. It stays faithful to UE: native paths, types,
field names, values, palette actions, compiler diagnostics, and editor
semantics remain native instead of being replaced by a parallel JSON model.

## Why Loomle

Text code already gives agents precise search, references, diffs, and edits.
Unreal assets do not. Loomle supplies that missing text workflow while keeping
the editor and UE APIs as the source of truth.

- Compact SAL Object Text uses fewer tokens than large generic JSON payloads.
- Queries start with summaries and local views instead of downloading an
  entire graph.
- Stable typed references make returned Nodes, Pins, Graphs, Widgets, and
  Blueprint objects safe to follow up.
- Palette and dynamic schema discovery let agents use capabilities that UE
  actually exposes instead of guessing constructors or fields.
- Dry runs share the real parse, resolve, validate, and plan path before any
  mutation is applied.
- Native compiler and object health diagnostics stay adjacent to the objects
  they describe.

## Current 0.7 Interface

The standalone Client exposes four MCP tools:

- `sal_query`: execute one self-contained SAL Query Text.
- `sal_patch`: execute one ordered SAL Patch Text.
- `sal_schema`: discover the resident SAL guide and the active interface cards.
- `editor_context`: read the user's current Unreal interaction target as SAL
  Object Text.

The current public SAL modules are Asset, Blueprint, Class, Graph, and Widget.
They cover Asset Registry discovery, Blueprint declarations and components,
Class reflection and defaults, graph-local flow and mutation, Widget trees,
factual reference queries, compilation, save, and editor context.

Example:

```sal
door = blueprint(asset: "/Game/Blueprints/BP_Door.BP_Door")

query door
summary
```

Use `sal_schema({})` for the active module index and
`sal_schema({ module: "graph" })` for one exact interface card. Supported exact
reads and Palette Entries provide dynamic discovery with `with schema`; each
interface card states the exact subjects that accept it.

## Install

Loomle 0.7 will ship only through Fab. The current public
[Fab listing](https://www.fab.com/listings/f0fb545c-b1d9-4525-8642-3f170134c428)
still distributes Loomle 0.6; the 0.7 package is not published yet.

The Fab plugin contains both components:

```text
LoomleBridge/
  Source/
  Resources/
    Loomle/
      <platform-arch>/
        loomle(.exe)
```

The executable under `Resources/Loomle` is the self-contained TypeScript/SAL
Client. It requires no separate Python, `uv`, Node.js, global Loomle install,
or project-local plugin copy.

The first accepted 0.7 QA target is Unreal Engine 5.7 on Apple Silicon macOS.
Before upgrading a project that used Loomle 0.6, close Unreal Editor and remove
or move its old `<Project>/Plugins/LoomleBridge` directory out of `Plugins`.
A same-named project plugin takes precedence over the Fab-installed engine
plugin and would keep loading 0.6. Back up local modifications first; Fab
cannot remove project files for you.

After the matching 0.7 Fab build is published, enable `LoomleBridge`, restart
Unreal Editor, and open the Loomle toolbar/status panel. The setup flow can
detect supported MCP hosts and copy exact configuration guidance for the
bundled Client; it never rewrites host configuration files automatically.

Full instructions: https://loomle.ai/install.html

## Quickstart

1. Open an Unreal project with the Fab-installed `LoomleBridge` enabled.
2. Open the Loomle status panel and copy the exact MCP host setup guidance.
3. Restart Codex, Claude, or the relevant MCP host.
4. Call `editor_context` to begin from the user's current editor state.
5. Use `sal_schema` when the target module or exact operation is unfamiliar.
6. Inspect with `sal_query`, dry-run changes with `sal_patch`, then apply and
   finalize through the owning asset.

See https://loomle.ai/quickstart.html for a complete first query and patch.

## Architecture

```text
Agent / MCP host
  -> bundled Loomle Client (stdio MCP + SAL)
  -> local runtime record
  -> LoomleBridge inside Unreal Editor
  -> UE 5.7 editor APIs and object model
```

The Client discovers live Bridge instances through
`~/.loomle/state/runtimes`. This directory is runtime discovery state, not a
global installation. When more than one compatible editor is online, Loomle
does not guess silently.

## Development

The repository has four current product boundaries:

- `sal/`: language parser, normalized model, schemas, and TypeScript SDK.
- `interfaces/`: resident guide and static UE interface cards.
- `client/`: standalone MCP Client.
- `engine/LoomleBridge/`: Unreal Editor plugin.

Install workspace dependencies and run the TypeScript test suite:

```sh
npm install
npm test
```

Build and test the native Client for the current supported runner:

```sh
npm run build:executable
npm run test:executable
```

Fab assembly consumes only the canonical program at
`.tmp/client/<platform-arch>/loomle(.exe)` and combines it with the Bridge
source plugin. See `packaging/client/` and `packaging/fab/` for the artifact
contracts.

## Documentation

- Website: https://loomle.ai/
- Install: https://loomle.ai/install.html
- Quickstart: https://loomle.ai/quickstart.html
- Interface overview: https://loomle.ai/tools/
- Releases: https://github.com/loomle/loomle/releases

Repository and product design documents live under `docs/`; canonical SAL and
UE interface contracts live under `sal/docs/` and `interfaces/`. Loomle 0.6
source and documents remain available from the `0.6` branch, its release tags,
and repository history; `main` contains only the current design and code.

## License

Loomle is open source under the MIT License. See [LICENSE](LICENSE).
