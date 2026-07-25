# widget

Inspect and edit authored `UWidget` objects in one
`UWidgetBlueprint::WidgetTree`.

## Target

Discovery may omit `id`; canonical exact Queries and every Patch use:

```sal
menu = target {
  domain: widget,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "11111111-1111-1111-1111-111111111111"
}
```

Widget and Blueprint are separate Domains even when they open the same
`UWidgetBlueprint`. They do not combine Query, Palette, identity, or mutation
surfaces.

Authored Widgets use Target-relative `@WidgetGuid`.

## Query

```sal
target
summary
tree [@widget-guid] [depth N]
widgets ["text"]
widget <name>
@widget-guid
references to <exact-subject> [in project]
palette entries ["text"]
palette @id
```

`summary` returns Widget root identity and source/reachable/detached counts.
`tree` returns authored structure and Slot layout, defaults to depth 20, and
does not expand nested User Widgets.

`widgets` includes detached source Widgets and supports exact filters on
`name`, `id`, `type`, `DisplayLabel`, `bIsVariable`, and `reachable`; ordering
keys are `name`, `type`, and `id`. Exact name and StableRef reads return the
shortest ancestor chain and may use `with schema`.

## Object Relationships

Widget objects are ordinary brace expressions:

```sal
stack.start = {
  id: "dddddddd-dddd-dddd-dddd-dddddddddddd",
  type: "/Script/UMG.Button",
  Slot: {
    type: "/Script/UMG.VerticalBoxSlot",
    Padding: "<FMargin native text>"
  }
}
```

`Slot` is native Panel placement state nested on the child. Named Slot state is
a relationship map:

```sal
area = {
  id: "eeeeeeee-eeee-eeee-eeee-eeeeeeeeeeee",
  type: "/Script/UMG.ExpandableArea",
  NamedSlots: {
    Header: @cccccccc-cccc-cccc-cccc-cccccccccccc,
    Body: null
  }
}
```

Panel Slots and Named Slots are not independent objects and have no StableRef,
query, Palette entry, or lifecycle. Unaddressable native field or slot names
are preserved in comments rather than silently renamed.

## Palette And Patch

```sal
query menu
palette @palette-entry-id
with schema
```

The result supplies ordinary creation fields:

```sal
start = { palette: "palette-entry-id" }
```

Widget Patch supports:

```sal
add root
add child to @panel-guid
add child before @anchor-guid
add child after @anchor-guid
add child to @host-guid.NamedSlots.Header
add child to menu.NamedSlots.Body

set @widget-guid.NativeField = value
reset @widget-guid.NativeField
set @widget-guid.Slot.NativeField = value
reset @widget-guid.Slot.NativeField

move @widget-guid to @panel-guid
move @widget-guid before @anchor-guid
move @widget-guid after @anchor-guid
move @widget-guid to @host-guid.NamedSlots.Header
remove @widget-guid

wrap @widget-guid with wrapper
wrap [@first-guid, @second-guid] with wrapper
replace @widget-guid with replacement

invoke @widget-guid Rename(displayName: "New Name")
invoke @widget-guid Duplicate() as copy
```

`add` materializes one Palette binding. `wrap` and Palette-backed `replace`
materialize their bindings themselves. `remove` deletes the authored subtree;
it never means detach. Exact Widget schema is authoritative for Widget and Slot
fields, constraints, operations, and cascades.

## Event And Finalization Handoffs

Multicast delegates such as `OnClicked` are Graph-event capabilities. Exact
Widget schema returns an independent Graph Target:

The following is a Result Text fragment, not a standalone Result Text document.

```sal
related eventGraph = target {
  domain: graph,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}
handoff graph_event to eventGraph
```

Widget defines no Event object or Event mutation. Graph owns the resulting
Node.

Compile and save require a separate Blueprint Target handoff. Do not mix
Widget edits and Blueprint finalization in one request.
