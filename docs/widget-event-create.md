# widget.event.create

## Intent

`widget.event.create` creates or returns the native Blueprint graph node for a
WidgetBlueprint widget event.

This tool maps to UE's component-bound event model. It creates
`UK2Node_ComponentBoundEvent` nodes bound by widget component property name and
multicast delegate property name. It does not create custom events and does not
mutate the WidgetTree hierarchy.

## Input

```json
{
  "assetPath": "/Game/UI/WBP_Menu",
  "widget": {
    "name": "CardButton"
  },
  "event": "OnClicked",
  "dryRun": false
}
```

`event` is the multicast delegate property name on the widget class, such as
`OnClicked` on `Button`.

## UE Mapping

UE's UMG details panel creates widget events by:

- resolving the widget as an `FObjectProperty` on the WidgetBlueprint class
- resolving the event as an `FMulticastDelegateProperty` on the widget class
- checking `FKismetEditorUtilities::FindBoundEventForComponent`
- creating `UK2Node_ComponentBoundEvent` in the WidgetBlueprint UberGraph

Loomle follows the same model. If the event node already exists, the tool
returns the existing node instead of creating a duplicate.

## Output

Success:

```json
{
  "isError": false,
  "assetPath": "/Game/UI/WBP_Menu",
  "widget": {
    "name": "CardButton"
  },
  "event": "OnClicked",
  "componentPropertyName": "CardButton",
  "delegatePropertyName": "OnClicked",
  "widgetClass": "/Script/UMG.Button",
  "created": true,
  "existing": false,
  "wouldCreate": true,
  "dryRun": false,
  "nodeId": "...",
  "graphName": "EventGraph",
  "graphRef": {
    "assetPath": "/Game/UI/WBP_Menu",
    "graphName": "EventGraph"
  },
  "node": {
    "nodeId": "...",
    "nodeClass": "K2Node_ComponentBoundEvent",
    "nodeClassPath": "/Script/BlueprintGraph.K2Node_ComponentBoundEvent",
    "graphName": "EventGraph",
    "graphRef": {
      "assetPath": "/Game/UI/WBP_Menu",
      "graphName": "EventGraph"
    }
  },
  "previousRevision": "...",
  "newRevision": "..."
}
```

In `dryRun=true`, no node is created. If no existing node is found, `wouldCreate`
is true and node fields are omitted.

Error:

```json
{
  "isError": true,
  "code": "WIDGET_EVENT_NOT_FOUND",
  "message": "WIDGET_EVENT_NOT_FOUND",
  "detail": "..."
}
```

Common error codes:

- `INVALID_ARGUMENT`
- `WIDGET_TREE_UNAVAILABLE`
- `WIDGET_NOT_FOUND`
- `WIDGET_COMPONENT_PROPERTY_NOT_FOUND`
- `WIDGET_CLASS_NOT_FOUND`
- `WIDGET_EVENT_NOT_FOUND`
- `WIDGET_EVENT_GRAPH_NOT_FOUND`
- `CREATE_WIDGET_EVENT_FAILED`
