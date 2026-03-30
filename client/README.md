# LOOMLE Client

This directory is the canonical home for the project-local Rust client.

Target role:
- provide the project-local `loomle` entrypoint installed under `Loomle/`
- discover the current Unreal project root
- connect to the project-local native MCP runtime in `LoomleBridge`
- expose a standard stdio MCP server to the host
- live in the installed project under `Loomle/`

## Build

```bash
cd client
cargo build
```

For local end-to-end validation during development, do not treat this checkout binary as the primary runtime entrypoint. Refresh the dev host project first, then validate through `<ProjectRoot>/Loomle/loomle(.exe)` by using:

```bash
python3 tools/dev_verify.py --project-root "/Path/To/Project"
```

## Runtime Role

```bash
cargo run -- --project-root "/Path/To/Project"
```

If `--project-root` is omitted, the client searches upward from the current
directory until it finds a `.uproject`.

This binary is no longer a multi-command CLI. It should only:

- accept a project root
- connect to the project-local Unreal runtime endpoint
- proxy standard MCP over stdio

Install and update are expected to move to scripts outside this
binary.
