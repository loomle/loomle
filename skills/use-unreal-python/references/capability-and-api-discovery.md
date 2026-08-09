# Capability and API Discovery

## Establish the live context

Call `status` before the first Python operation. Require the intended project
to be bound and the Bridge to be ready. Resolve a missing or ambiguous binding
through `project`; do not let a Python script guess which Editor owns the task.

Record relevant Editor state such as PIE, simulation, compilation, asset
loading, or shutdown when it changes which objects or APIs are safe. Load the
`debug-unreal-pie-with-python` Skill before entering PIE.

## Select the capability owner

Before choosing Python:

1. inspect the installed SAL modules with `sal_schema`;
2. use `sal_query` for structured discovery and current-state inspection;
3. use `sal_patch` when the owning interface provides validation, dry run, or
   stable identity;
4. use `editor` for its supported presentation operations;
5. load any matching resident domain Skill.

Use Python only for the remaining native capability gap. A structured failure
is not permission to bypass the interface with Python.

## Probe the live Unreal Python surface

Unreal Python exposure varies by engine version, plugin set, and reflected
metadata. Confirm APIs in the bound Editor with a read-only probe before a
mutating call. Prefer `hasattr`, `getattr`, a filtered `dir`, known subsystem
lookups, and harmless reads. Return a compact projection rather than an entire
module listing.

```python
import unreal

def run():
    type_name = "AssetToolsHelpers"
    exposed_type = getattr(unreal, type_name, None)
    methods = [] if exposed_type is None else [
        name for name in dir(exposed_type)
        if "asset" in name.lower()
    ][:40]
    return {
        "engineVersion": unreal.SystemLibrary.get_engine_version(),
        "type": type_name,
        "available": exposed_type is not None,
        "matchingMembers": methods,
    }
```

When a target object already exists, probe that exact object's class and
current values. Do not guess an editor-property name when the live object can
be inspected first. If the required API is unavailable, return an actionable
result naming the engine version, missing type or member, and safe next step.

## Bound discovery risk

- Do not call a suspected mutator merely to learn whether it exists.
- Do not dump huge `dir(unreal)` results or enumerate the whole object graph.
- Do not maintain a permanent broad blacklist from one engine version.
- Treat synchronous APIs as potentially sensitive to execution context; use
  Loomle's normal safe ticker entry and preserve a reproducible minimal call
  when a native API fails.
