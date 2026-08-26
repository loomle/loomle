# Create, Edit, and Duplicate Assets

## Preflight identity and capability

Use the Asset Domain to search the exact destination before Python. Define the
destination in UE terms:

- package path: `/Game/LoomleWork`;
- asset name: `M_AgentGenerated`;
- object path: `/Game/LoomleWork/M_AgentGenerated.M_AgentGenerated`;
- expected native Class: for example `/Script/Engine.Material`.

Confirm the live factory, asset Class, and relevant editor properties with a
read-only Python probe. Factories and reflected property names vary by asset
type, enabled plugin, and engine version. Do not create an asset merely to
discover whether a factory works.

## Create or reuse without overwrite

The portable policy across UE 5.7 and 5.8 is check, load, verify, then create.
UE 5.8 adds an overwrite option to native `CreateAsset`, but UE 5.7 does not;
the Skill never treats overwrite as an idempotency mechanism.

```python
import unreal

def run():
    package_path = "/Game/LoomleWork"
    asset_name = "M_AgentGenerated"
    object_path = f"{package_path}/{asset_name}.{asset_name}"
    expected_class = "/Script/Engine.Material"

    asset = unreal.load_object(None, object_path)
    created = False
    if asset is not None:
        actual_class = asset.get_class().get_path_name()
        if actual_class != expected_class:
            return {
                "ok": False,
                "conflict": True,
                "objectPath": object_path,
                "expectedClass": expected_class,
                "actualClass": actual_class,
            }
    else:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name,
            package_path,
            unreal.Material,
            unreal.MaterialFactoryNew(),
        )
        created = asset is not None

    if asset is None:
        return {"ok": False, "objectPath": object_path, "reason": "creation failed"}
    return {
        "ok": True,
        "objectPath": asset.get_path_name(),
        "classPath": asset.get_class().get_path_name(),
        "created": created,
        "reused": not created,
        "asset": sal.object(asset),
    }
```

Adapt the Class and factory to the task after live API discovery. Do not copy
the example destination into a formal project location without authorization.

## Edit current state, not remembered state

Reload the exact object path in the edit call. For every requested property:

1. confirm the reflected property exists and is editable;
2. read and JSON-project the current value;
3. normalize and compare it with the requested value;
4. call the asset type's native editor API when one exists, otherwise use the
   reflected editor-property setter;
5. read back the value in the same call;
6. report `changed` or `alreadySatisfied`.

Do not assume a generic `set_editor_property` supplies the specialized compile,
rebuild, post-edit, or validation behavior owned by a domain-specific API.
Hand Blueprint compilation, PCG Graph editing, StateTree editing, and other
published domain work back to their structured interfaces.

## Duplicate deliberately

Before duplication, require one exact source, an absent destination, and a
verified expected Class. Use Asset Tools or Editor Asset Library duplication,
not filesystem copying. Afterward verify that source and destination both
exist as distinct Asset paths with the expected Class. Treat an existing
destination as a conflict unless the user explicitly chose a different policy;
do not replace it as an implicit retry.

## Persist and verify

After successful in-memory creation or editing:

1. rediscover the exact target with Asset SAL;
2. use the exact Asset terminal `save` when available;
3. query the Asset again and use a separate read-only Python call for
   properties Asset Registry does not publish;
4. report the structured save result separately from the mutation result.

Saving the UObject inside the create call makes partial failure harder to
classify. Keep it separate unless the native asset type requires one atomic
factory-owned workflow and that behavior has been confirmed.
