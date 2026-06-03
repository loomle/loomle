# widget.tree.inspect

## Intent

`widget.tree.inspect` returns the designer `UWidgetTree` for a
`UWidgetBlueprint` in UE terms. It is the tree snapshot an agent uses before
editing hierarchy, parentage, slot layout, or widget references.

This tool does not describe arbitrary widget properties. Use `widget.inspect`
for class and instance property descriptors and current values.

## UE Mapping

The tool follows UE's native UMG structure:

- `UWidgetBlueprint::WidgetTree` owns the designer tree.
- `UWidgetTree::RootWidget` is the primary hierarchy root.
- `UPanelWidget` owns ordered child widgets.
- A child widget stores its parent layout through `UWidget::Slot`.
- `UPanelSlot::Parent` and `UPanelWidget::GetChildIndex` define parent and
  index.
- `UWidget::bIsVariable` and
  `UWidgetBlueprint::WidgetVariableNameToGuidMap` define Blueprint variable
  exposure.

## Input

```json
{
  "assetPath": "/Game/UI/WBP_Menu",
  "view": "outline | layout",
  "filter": {
    "names": ["TitleText"],
    "text": "textblock"
  }
}
```

`assetPath` is required.

`view` defaults to `outline`.

`filter` is optional. When present, the response keeps `rootWidget` intact and
adds `matches`.

## Views

### outline

Returns hierarchy and widget identity without slot property values.

Each widget node includes:

- `name`
- `widgetClass`
- `parentName`
- `index`
- `isVariable`
- `variableGuid`
- `children`

`parentName`, `index`, and `variableGuid` are `null` when UE has no value for
them.

### layout

Returns the outline fields plus layout information for each child widget:

- `slotClass`
- `slot`

`slot` contains editable non-transient, non-private properties exported with
UE's property text format. Root widgets normally have no slot.

## Output

Success:

```json
{
  "isError": false,
  "assetPath": "/Game/UI/WBP_Menu",
  "revision": "...",
  "view": "outline",
  "rootWidget": {},
  "matches": [],
  "diagnostics": []
}
```

`matches` is only present when a filter was supplied.

Error:

```json
{
  "isError": true,
  "code": "WIDGET_TREE_UNAVAILABLE",
  "message": "WIDGET_TREE_UNAVAILABLE",
  "detail": "..."
}
```

## Notes

`details` is intentionally not a view. The previous behavior only meant
"filtered matches" and overlapped with `widget.inspect`, so filtering is now a
view-independent feature.
