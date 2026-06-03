# Widget Tree Edit

`widget.tree.edit` mutates a WidgetBlueprint's `WidgetTree`.

The tool follows the shared
[`Mutation Dry Run Contract`](MUTATION_DRY_RUN_CONTRACT.md): `dryRun=true`
parses, resolves, validates, and plans the same operation sequence as a real
edit, then stops before mutating UE state.

## Boundary

`widget.tree.edit` owns WidgetTree structure and instance property edits:

- create a widget from a `widget.palette` entry
- remove a widget subtree
- rename a widget
- set one widget or slot property
- move a widget under another panel widget

It does not create Blueprint graph event nodes. Use `widget.event.create` for
component-bound widget events.

## Inputs

```json
{
  "assetPath": "/Game/UI/WBP_Menu",
  "commands": [],
  "dryRun": false,
  "expectedRevision": "optional-current-tree-revision"
}
```

`returnDiff`, `returnDiagnostics`, `idempotencyKey`, and `continueOnError` are
not part of this contract. Diagnostics and diff are returned when available,
and command batches stop at the first invalid command without partially
continuing past a known failure.

## Commands

`addFromPalette` creates one widget from a palette entry. Public calls should
pass the full selected `widget.palette` entry rather than guessing classes.

```json
{
  "kind": "addFromPalette",
  "entry": {
    "id": "widget.palette:TextBlock",
    "payload": { "widgetClass": "/Script/UMG.TextBlock" }
  },
  "name": "TitleText",
  "parent": { "name": "RootCanvas" },
  "slot": {}
}
```

`removeWidget` removes one widget. Removing a panel removes its child widgets.

```json
{ "kind": "removeWidget", "target": { "name": "TitleText" } }
```

`renameWidget` renames one widget while preserving its object, slot, property
state, bindings, and references where UE exposes the required update path.

```json
{
  "kind": "renameWidget",
  "target": { "name": "WorldSelect_Card0_Button" },
  "name": "CardButton"
}
```

`setProperty` writes one editable property on the widget, or on its current
slot when the widget class does not own the property.

```json
{
  "kind": "setProperty",
  "target": { "name": "TitleText" },
  "property": "Text",
  "value": "Hello"
}
```

`reparentWidget` moves one widget under a different parent panel widget and can
apply slot properties for the new slot.

```json
{
  "kind": "reparentWidget",
  "target": { "name": "TitleText" },
  "newParent": { "name": "ContentBox" },
  "slot": {}
}
```

## Result

Successful dry runs and successful applies return the same structural envelope:

```json
{
  "isError": false,
  "valid": true,
  "dryRun": true,
  "applied": false,
  "assetPath": "/Game/UI/WBP_Menu",
  "operation": "widget.tree.edit",
  "previousRevision": "rev-a",
  "newRevision": "rev-a",
  "planned": {
    "tool": "widget.tree.edit",
    "operation": "widget.tree.edit",
    "commandCount": 1,
    "commands": []
  },
  "resolvedRefs": {},
  "diff": {
    "scope": "widget.tree",
    "changes": []
  },
  "opResults": [],
  "diagnostics": []
}
```

On `expectedRevision` mismatch, the result uses `REVISION_CONFLICT` and keeps
the current revision in both `previousRevision` and `newRevision`.

## UE Mapping

UMG does not treat the `WidgetTree` as a standalone tree. Source widgets are
also widget variables tracked by `UWidgetBlueprint::WidgetVariableNameToGuidMap`.
UE's compiler expects every source widget to already have a stable variable
GUID and emits ensures when direct tree mutation skips that step.

Loomle therefore maps edits to UE semantics as follows:

- added widgets call `UWidgetBlueprint::OnVariableAdded`
- renamed widgets call `UWidgetBlueprint::OnVariableRenamed`
- removed widgets and removed descendants call `UWidgetBlueprint::OnVariableRemoved`
- structural changes call `FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified`
- dry runs validate these same targets without calling mutation APIs

This keeps WidgetTree edits aligned with UMG Designer and avoids relying on the
compiler's GUID repair path as normal behavior.
