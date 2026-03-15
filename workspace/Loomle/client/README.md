# LOOMLE Client

This directory is the install target for the project-local LOOMLE client.

Target installed runtime:

```text
Loomle/client/loomle(.exe)
```

The client is the project-local entrypoint for agent and user workflows. It belongs to the workspace layer, not the Unreal plugin layer.

Preferred commands:

- `loomle doctor`
- `loomle list-tools`
- `loomle call <tool-name> --args '<json-object>'`
- `loomle session`

Install commands:

- `loomle install`
- `loomle install --plugin-mode source`

Support commands:

- `loomle server-path`

Guidance:

- Prefer `list-tools` over hardcoded tool assumptions.
- Prefer `call` for one-shot requests.
- Prefer `session` for repeated requests, integrations, and load tests.
- `loomle install` defaults to `--plugin-mode prebuilt` for faster end-user installs. Use `--plugin-mode source` when the project runs against a source-built Unreal Engine and you want local plugin recompiles to stay available.

## Session Mode

`loomle session` starts a persistent client session over stdin/stdout.

Shape:

- write one JSON request per line to stdin
- read one JSON response per line from stdout
- use `tools/list` to discover tools
- use `tools/call` to invoke a tool repeatedly without restarting the client

Minimal requests:

```json
{"id":1,"method":"tools/list"}
{"id":2,"method":"tools/call","params":{"name":"context","arguments":{}}}
```

Request forms:

- `{"id":1,"method":"tools/list"}`
- `{"id":2,"method":"tools/call","params":{"name":"graph.query","arguments":{"assetPath":"/Game/Test","graphName":"EventGraph","graphType":"blueprint"}}}`

Response shape:

- every response includes the same `id`
- successful responses include `"ok": true` and a `result`
- failed responses include `"ok": false` and an `error`

Use `call` when you only need one request. Use `session` when you want to keep a single LOOMLE client process open and send multiple requests through it.
