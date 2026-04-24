# LOOMLE Graph Reference Workspace

This directory is repository-internal reference material for graph development
and tests.

It is not installed into Unreal projects. Release bundles must not include this
workspace, and agents should not treat it as a runtime entrypoint.

## Current Use

- graph-domain guides, semantics, catalogs, and examples
- smoke/regression fixture validation
- product development reference while Blueprint, Material, and PCG tools are
  split into domain-specific MCP namespaces

## Runtime Entry

Use the global MCP command:

```bash
loomle mcp
```

Attach to a running Unreal project through MCP:

1. `project.list`
2. `project.attach`
3. domain-specific UE tools exposed by `LoomleBridge`

Project support is installed or upgraded with MCP `project.install`, which
copies only `Plugins/LoomleBridge/` into the target project.

## Directory Map

- `blueprint/`: Blueprint graph reference material
- `material/`: Material graph reference material
- `pcg/`: PCG graph reference material
- `widget/`: UMG widget-tree reference material
- `*/catalogs/`: static catalogs used by tests and planning
- `*/examples/`: JSON payload examples used by tests
