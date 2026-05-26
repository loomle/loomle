# Blueprint Member Edit

`blueprint.member.edit` edits Blueprint-owned definitions: variables,
functions, macros, dispatchers, events, and components. It should not be used
for graph-local node placement or pin wiring.

## Design Intent

Member edits must preserve Unreal Engine member semantics. Creating a normal
Blueprint function is not the same operation as overriding an inherited native
Blueprint event/function. Loomle exposes those as separate operations so agents
do not accidentally create a same-named user function graph that does not
participate in runtime dispatch.

## Function Operations

### `function.create`

Creates a new user-authored Blueprint function graph. This maps to
`FBlueprintEditorUtils::AddFunctionGraph` with `bIsUserCreated=true` and no
inherited signature source.

Use this only for new functions owned by the Blueprint.

### `function.override`

Creates or confirms a Blueprint implementation graph for an inherited
Blueprint-overridable function, including native `BlueprintNativeEvent` and
`BlueprintImplementableEvent` functions that UE represents as function graphs.

Request:

```json
{
  "memberKind": "function",
  "operation": "override",
  "args": {
    "functionName": "GetBodyMesh",
    "ownerClassPath": "/Script/Oasium.OasiumAvatarBase"
  }
}
```

Fields:

- `functionName` or `name`: required inherited function name.
- `ownerClassPath`: optional expected declaring class. When supplied, Loomle
  validates that UE resolves the override to that class or one of its generated
  authoritative equivalents.

Behavior:

- Resolve the override using UE's override lookup path:
  `FBlueprintEditorUtils::GetOverrideFunctionClass`.
- Validate the resolved `UFunction` with
  `UEdGraphSchema_K2::CanKismetOverrideFunction`.
- If the override graph already exists, treat the operation as idempotent.
- If a same-named graph exists but is a normal user function graph, reject it
  instead of silently treating it as an override.
- Otherwise create a new graph named after the function and add it with
  `FBlueprintEditorUtils::AddFunctionGraph(Blueprint, Graph, false,
  OverrideFunctionClass)`.
- Do not apply manual signature edits; UE creates the entry/result terminators
  from the inherited signature.

Errors:

- `function override requires functionName.`
- `Failed to resolve inherited override function: <name>.`
- `Function is not Blueprint-overridable: <name>.`
- `Override function owner does not match ownerClassPath.`
- `Graph already exists but does not represent the requested override.`

## UE Implementation Model

UE's My Blueprint override menu first resolves the inherited function's
declaring class with `FBlueprintEditorUtils::GetOverrideFunctionClass`. For
function-style overrides it creates a new K2 graph and calls
`FBlueprintEditorUtils::AddFunctionGraph` with `bIsUserCreated=false` and the
resolved signature class. That is the source of truth for `function.override`.

Loomle must not synthesize an override by calling `function.create` and copying
pins. That creates a normal user function graph and can compile while native
dispatch still calls the parent implementation.

## Audit Notes

Issue #154 exposed a UE semantics gap rather than a generic graph creation
failure. Acceptance requires checking that an inherited
`BlueprintNativeEvent` with a return value creates an override graph whose
compiled Blueprint dispatches through the Blueprint implementation.
