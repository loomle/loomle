# widget_edit

## Intent

`widget_edit` mutates properties on existing WidgetTree widget instances.

It is separate from `widget_tree_edit`:

- `widget_tree_edit` changes hierarchy: create, remove, rename, reparent.
- `widget_edit` changes widget or slot property values.

Use `widget_tree_inspect` to find widget instances, then `widget_inspect` to
discover writable property names and serialized value formats.

## Input

```json
{
  "assetPath": "/Game/UI/WBP_Menu",
  "commands": [
    {
      "kind": "setProperty",
      "widget": { "name": "TitleText" },
      "property": "Text",
      "value": "Hello"
    },
    {
      "kind": "setSlotProperty",
      "widget": { "name": "TitleText" },
      "property": "ZOrder",
      "value": "2"
    }
  ],
  "dryRun": false,
  "expectedRevision": "..."
}
```

`commands[]` is an ordered batch. Every command is preflighted before mutation;
if any command is invalid, nothing is applied.

## Commands

### setProperty

Sets one reflected property on the widget instance itself.

### setSlotProperty

Sets one reflected property on the widget's current `UPanelSlot`.

Slot properties depend on the parent panel. Use
`widget_tree_inspect view="layout"` or instance-mode `widget_inspect` to see the
current slot class and values.

## Output

The result uses the standard mutation envelope:

```json
{
  "isError": false,
  "valid": true,
  "applied": true,
  "dryRun": false,
  "assetPath": "/Game/UI/WBP_Menu",
  "previousRevision": "...",
  "newRevision": "...",
  "planned": {},
  "diff": {},
  "diagnostics": [],
  "opResults": []
}
```

Errors return `isError: true`, `code`, `message`, and `opResults` when the
failure is command-specific.

## Revision

Widget revisions include hierarchy, slot layout, and reflected widget property
values. A successful `widget_edit` property mutation updates the revision.
