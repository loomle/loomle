# widget.inspect

## Intent

`widget.inspect` describes editable UMG widget properties. It complements
`widget.tree.inspect`: the tree tool returns hierarchy and layout context, while
this tool returns property descriptors and current instance values.

## Input Modes

Class mode:

```json
{
  "widgetClass": "TextBlock"
}
```

Instance mode:

```json
{
  "assetPath": "/Game/UI/WBP_Menu",
  "widget": {
    "name": "TitleText"
  }
}
```

`widgetClass` may also be provided in instance mode as an optional validation
hint. The inspected class still comes from the actual widget instance. If the
instance is not assignable to the requested class, the tool returns
`WIDGET_CLASS_MISMATCH`.

## UE Mapping

- `widgetClass` resolves to a `UWidget` subclass by full path or short class
  name.
- Instance mode loads `UWidgetBlueprint::WidgetTree` and finds the widget by
  designer name.
- Editable widget properties come from reflected `FProperty` entries on the
  inspected widget class.
- Slot properties come from the instance's `UWidget::Slot`, when present.
- Values are exported with UE's text property serialization format, matching the
  format accepted by `widget.tree.edit` `setProperty`.

## Output

Success:

```json
{
  "isError": false,
  "widgetClass": "/Script/UMG.TextBlock",
  "assetPath": "/Game/UI/WBP_Menu",
  "widget": {
    "name": "TitleText"
  },
  "properties": [
    {
      "name": "Text",
      "type": "FText",
      "category": "Content",
      "writable": true
    }
  ],
  "slotClass": "/Script/UMG.CanvasPanelSlot",
  "slotProperties": [],
  "currentValues": {},
  "slotCurrentValues": {}
}
```

`assetPath`, `widget`, `slotClass`, `currentValues`, and `slotCurrentValues` are
only present in instance mode. `slotClass` is `null` when the instance has no
slot.

Error:

```json
{
  "isError": true,
  "code": "WIDGET_NOT_FOUND",
  "message": "WIDGET_NOT_FOUND",
  "detail": "..."
}
```

Common error codes:

- `INVALID_ARGUMENT`
- `WIDGET_CLASS_NOT_FOUND`
- `WIDGET_CLASS_MISMATCH`
- `WIDGET_NOT_FOUND`
- `WIDGET_TREE_UNAVAILABLE`
