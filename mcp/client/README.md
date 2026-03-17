# LOOMLE Client

This directory is the canonical home for the project-local Rust client.

Target role:
- provide the machine-level `loomle install` entrypoint used after bootstrap
- provide the single supported LOOMLE client entrypoint
- discover the current Unreal project root
- locate the MCP server installed under `Plugins/LoomleBridge/Tools/mcp/...`
- connect to the project-local MCP server/runtime
- live in the installed project under `Loomle/`

## Build

```bash
cd mcp/client
cargo build
```

For local end-to-end validation during development, do not treat this checkout binary as the primary runtime entrypoint. Refresh the dev host project first, then validate through `<ProjectRoot>/Loomle/loomle(.exe)` by using:

```bash
python3 tools/dev_verify.py --project-root "/Path/To/Project"
```

## Commands

```bash
cd mcp/client
cargo run -- install --project-root "/Path/To/Project" --manifest-path "/Path/To/manifest.json"
cargo run -- update --project-root "/Path/To/Project"
cargo run -- update --project-root "/Path/To/Project" --apply
cargo run -- doctor --project-root "/Path/To/Project"
cargo run -- skill list
cargo run -- skill list --installed
cargo run -- skill install material-weaver
cargo run -- skill remove material-weaver
cargo run -- list-tools --project-root "/Path/To/Project"
cargo run -- call context --project-root "/Path/To/Project"
cargo run -- call diag.tail --project-root "/Path/To/Project" --args '{"fromSeq":0,"limit":50}'
cargo run -- call graph.verify --project-root "/Path/To/Project" --args '{"mode":"runtime","graphType":"pcg","componentPath":"/Game/Maps/MyMap.MyMap:PersistentLevel.PCGVolume_0.PCGComponent0"}'
cargo run -- session --project-root "/Path/To/Project"
```

If `--project-root` is omitted, the client searches upward from the current directory until it finds a `.uproject`.

`loomle update` checks the installed version against the published latest release. `loomle update --apply` upgrades in place. If Unreal Editor is already running, restart it afterward so the editor loads the updated LoomleBridge plugin version.

`loomle install` always installs both the prebuilt plugin binaries and the plugin source so Unreal can load quickly and still participate in local target rebuilds. If Unreal Editor is already running, restart it afterward so the editor loads the newly installed LoomleBridge plugin version.

`loomle skill ...` manages official LOOMLE skills from the published `loomle/skills` registry. These commands install into the local Codex skills directory and do not require `--project-root`.

`diag.tail` reads persisted LoomleBridge diagnostics incrementally. Treat `fromSeq` as an exclusive cursor and reuse the returned `nextSeq` on the next poll. Persisted events live under `Loomle/runtime/diag/diag.jsonl`.

`graph.verify` is the verification primitive that closes the graph loop. Today it supports `mode="health"`, `mode="compile"`, and `mode="runtime"`. `runtime` currently supports `graphType="pcg"` only. Use `graph.verify(mode="runtime")` after regenerating a PCG component when you need generated output summaries, managed spawned actors/components, or per-node execution summaries. For spawner-heavy graphs, prefer `managedResources` over `generatedGraphOutput` when the latter is empty or sparse.

## Visual loop

For editor-driven visual verification, use the project-local client to:

1. open an asset editor
2. focus the semantic panel you care about
3. capture the current editor window

Example flow:

```bash
cargo run -- call editor.open --project-root "/Path/To/Project" --args '{"assetPath":"/Game/MyFolder/MyBlueprint"}'
cargo run -- call editor.focus --project-root "/Path/To/Project" --args '{"assetPath":"/Game/MyFolder/MyBlueprint","panel":"graph"}'
cargo run -- call editor.screenshot --project-root "/Path/To/Project" --args '{}'
```

`editor.focus` intentionally uses semantic panel names such as `graph`, `viewport`, `details`, `palette`, `find`, `preview`, `log`, `profiling`, `constructionScript`, and `myBlueprint` instead of raw Unreal tab ids.

## Session mode

`loomle session` keeps a single client process open and accepts one JSON request per line on stdin.

Prefer `session` over repeated one-shot calls when you expect high-concurrency or high-volume query traffic. It avoids repeated client startup and is noticeably more efficient.

Minimal examples:

```json
{"id":1,"method":"tools/list"}
{"id":2,"method":"tools/call","params":{"name":"context","arguments":{}}}
```

Each response is emitted as one JSON line on stdout with the same `id`.
