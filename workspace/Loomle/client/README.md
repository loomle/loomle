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
