# Idempotent Mutation and Verification

## Design restartable calls

Make each mutating script safe to resume from current Editor state:

- resolve the target from a stable package path, object path, class path, GUID,
  or other durable identity inside every call;
- check whether an object already exists before creating it;
- compare current values before setting them;
- return an already-satisfied state as success;
- separate creation, stable rediscovery, configuration, saving, and readback
  when later steps would otherwise depend on temporary UObject identity;
- keep unrelated asset creation, graph editing, compilation, saving, and
  validation in different calls.

Idempotency is a property of the script's logic, not a guarantee supplied by
the `python` tool.

## Return structured evidence

Return a top-level dictionary composed only of JSON-compatible values. Include
facts that let another call or the user verify the outcome:

- canonical target paths and class paths;
- `created`, `reused`, `changed`, `alreadySatisfied`, `saved`, and `skipped`
  states where applicable;
- before and after values;
- warnings and bounded counts.

Never return a UObject wrapper. Native logs are diagnostics, not the result.

## Create or reuse, then configure

Use one call to create or resolve an asset and return its stable path. Use a
later call to reload that path before applying dependent configuration.

```python
import unreal

def run():
    package_path = "/Game/LoomleWork"
    asset_name = "M_AgentGenerated"
    object_path = f"{package_path}/{asset_name}.{asset_name}"
    asset = unreal.load_object(None, object_path)
    created = False
    if asset is None:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name,
            package_path,
            unreal.Material,
            unreal.MaterialFactoryNew(),
        )
        created = asset is not None
    if asset is None:
        return {
            "ok": False,
            "objectPath": object_path,
            "reason": "create_asset returned None",
        }
    return {
        "ok": True,
        "objectPath": asset.get_path_name(),
        "classPath": asset.get_class().get_path_name(),
        "created": created,
        "reused": not created,
    }
```

Use task-specific paths and authorization. Do not copy this example into a
formal asset location without confirming the intended destination.

## Persist and verify explicitly

Creation or property mutation in memory does not prove durable persistence.
When the task requires saving:

1. save the exact asset or package explicitly;
2. report the native save result;
3. reload or independently query the target;
4. verify important class and property values;
5. use the owning structured interface for compilation or specialized checks.

Do not save unrelated dirty packages. A successful `run()` proves only what
its returned evidence and subsequent readback establish.
