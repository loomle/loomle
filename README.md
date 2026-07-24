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

The standalone Client exposes six MCP tools:

- `status`: inspect the Client version, update availability, and bound session
  and Bridge health.
- `project`: inspect project availability and bind this MCP session to one
  Unreal project.
- `sal_query`: execute one self-contained SAL Query Text.
- `sal_patch`: execute one ordered SAL Patch Text.
- `sal_schema`: discover the resident SAL guide and the active interface cards.
- `editor_context`: read the user's current Unreal interaction target as SAL
  Object Text.

The current public SAL modules are Asset, Blueprint, Class, Graph, StateTree,
and Widget. They cover Asset Registry discovery, Blueprint declarations and
components, Class reflection and defaults, graph-local flow and mutation,
StateTree hierarchy and bindings, Widget trees, factual reference queries,
compilation, save, and editor context.

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

Loomle 0.7.0-rc.1 is available from
[GitHub Releases](https://github.com/loomle/loomle/releases/tag/v0.7.0-rc.1)
for Unreal Engine 5.7:

| Platform | Architecture | Package |
| --- | --- | --- |
| macOS | Apple Silicon | `loomle-fab-plugin-darwin-arm64.zip` |
| Windows | x64 | `loomle-fab-plugin-win32-x64.zip` |

The public
[Fab listing](https://www.fab.com/listings/f0fb545c-b1d9-4525-8642-3f170134c428)
still distributes Loomle 0.6. Do not use that package with the 0.7
documentation.

Each platform archive contains both components:

```text
LoomleBridge/
  Source/
  Resources/
    Loomle/
      <platform-arch>/
        loomle(.exe)
```

The executable under `Resources/Loomle` is the self-contained SAL Client. It
requires no separate Python, `uv`, Node.js, global Loomle install, or
project-local Client.

Before upgrading from 0.6, close Unreal Editor and remove or move the old
`<Project>/Plugins/LoomleBridge` directory. A same-named project plugin takes
precedence over the new engine plugin and would keep loading 0.6.

Extract the matching archive, then copy the complete `LoomleBridge` directory
to:

```text
<UE_5.7>/Engine/Plugins/Marketplace/LoomleBridge
```

Enable `LoomleBridge`, restart Unreal Editor, and configure an MCP server named
`loomle` to launch the bundled Client with the argument `mcp`:

```text
macOS:   LoomleBridge/Resources/Loomle/darwin-arm64/loomle
Windows: LoomleBridge/Resources/Loomle/win32-x64/loomle.exe
```

Full instructions: https://loomle.ai/install.html

## Quickstart

1. Open an Unreal project with `LoomleBridge` enabled.
2. Restart Codex, Claude, or the relevant MCP host after configuring Loomle.
3. Call `status` once to inspect Client, update, session, and Bridge health.
4. If the session is unbound, call `project` to inspect or bind the intended
   project.
5. Call `editor_context` to begin from the user's current editor state.
6. Use `sal_schema` when the target module or exact operation is unfamiliar.
7. Inspect with `sal_query`, dry-run changes with `sal_patch`, then apply and
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
global installation. Each MCP session keeps one sticky project binding and
validates that project's current Editor with a short live health probe. If the
bound project is offline, Loomle reports that state and never falls through to
another online project. Use `project({})` to inspect candidates and bind or
switch explicitly when needed.

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

Run the complete Loomle UE Automation category against a same-commit
development plugin compiled with the native tests:

```sh
npm run test:ue-automation -- \
  --ue-root <UE-root> \
  --project-template tests/fixtures/ue/LoomleTestHost \
  --plugin-dir <compiled-same-commit-test-plugin> \
  --output-dir <new-artifact-directory> \
  --target darwin-arm64
```

The output directory must not already exist. Raw plugin source is not a
runnable candidate. The final release archive intentionally excludes native
test code, so it is not the complete Automation candidate.

Run the small packaged Client-to-UE workflow against the exact audited release
archive and its bundled Client:

```sh
npm run test:packaged-e2e -- \
  --ue-root <UE-root> \
  --project-template tests/fixtures/ue/LoomleTestHost \
  --plugin-archive <final-release-candidate.zip> \
  --output-dir <new-artifact-directory> \
  --target darwin-arm64
```

Release assembly consumes only the canonical program at
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
