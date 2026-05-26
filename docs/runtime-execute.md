# Runtime Execute

## Intent

`execute` is the Unreal-side Python escape hatch for editor tasks that do not
yet have a dedicated Loomle semantic tool.

It should stay honest about its boundary: user Python still runs inside the
editor process, so Loomle cannot make arbitrary Unreal Python code crash-proof.
Where UE has known fatal paths that are easy to preflight without changing
normal semantics, Loomle should fail earlier with a Python exception and a clear
diagnostic.

## Asset Load Guard

UE `CreatePackage` fatals if a package name contains double slashes. A common
agent mistake is to normalize every `AssetData.package_name` under `/Game`,
turning an engine asset such as:

```text
/Engine/Tutorial/SubEditors/TutorialAssets/TutorialAnimationBlueprint
```

into:

```text
/Game//Engine/Tutorial/SubEditors/TutorialAssets/TutorialAnimationBlueprint
```

For `mode="exec"`, Loomle wraps `unreal.load_asset` and
`unreal.EditorAssetLibrary.load_asset` when those Python attributes are
assignable. The wrapper rejects string paths that start with `/` and contain
`//` before calling UE load APIs.

The guard raises:

```text
LOOMLE_INVALID_PACKAGE_PATH
```

This is intentionally narrow. It does not validate every UE package-name rule
and it does not change structured asset tools, which already validate asset
paths before mutation.

## Recovery Model

If the editor process crashes during arbitrary Python execution, the active RPC
connection may disappear before Loomle can produce a normal execute result.
After reconnect, callers should inspect:

- `loomle.status` for attachment and runtime health
- `diagnostic.tail` for recent structured Loomle errors
- `log.tail` for Unreal output log context
- project `Saved/Crashes` for UE crash context when available

If `execute` reports `Python runtime is not initialized`, retry after the editor
finishes loading. Loomle calls `ForceEnablePythonAtRuntime()` before returning
that error, so repeated failure means the editor-side Python plugin/runtime is
not yet recoverable in the current process.

## Audit Notes

- #144 confirmed UE's fatal double-slash package path behavior by reading
  `Runtime/CoreUObject/Private/UObject/UObjectGlobals.cpp`.
- The implemented guard is defensive preflight around common asset-load helper
  calls, not a sandbox for arbitrary Python.
