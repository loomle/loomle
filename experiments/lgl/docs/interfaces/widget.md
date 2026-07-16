# widget

Inspect and edit authored `UWidget` objects inside one
`UWidgetBlueprint::WidgetTree`. Widget composes the `blueprint` interface on the
same target and assumes the resident LGL Core guide.

## Target

Use the exact WidgetBlueprint locator. The first discovery Query may omit `id`;
later exact Queries and every Patch require both fields:

```lgl
menu = blueprint(
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "blueprint-guid"
)
```

There is no Widget document target or `widget(asset: ...)` wrapper.
`widget@id` is scoped inside `menu` and cannot replace it. Widget and Blueprint
share one target-specific `summary` and Palette.

## Query

Every Query starts with `query menu` and chooses exactly one primary operation:

```lgl
summary
tree [widget@id] [depth N]
widgets ["text"]
widget <name>
widget@id
palette entries ["text"]
palette @id
```

`summary` combines the Blueprint directory with the root identity and source,
reachable, and detached Widget counts. `tree` returns the authored structure
and Slot layout skeleton, defaults to depth 20, and marks truncated boundaries
with comments. A nested User Widget is one leaf; query its WidgetBlueprint
separately.

`widgets` discovers all source Widgets, including detached objects, and returns
compact identities. Search covers object and display names plus native Class
identity. Cursor pagination defaults to 50. Its closed clauses are:

| Clause | Fields | Behavior |
| --- | --- | --- |
| `where` | `name`, `id`, `type`, `DisplayLabel` | exact `=` and `!=` |
| `where` | `bIsVariable`, `reachable` | `=`, `!=`, boolean shorthand |
| `order by` | `name`, `type`, `id` | ascending or descending |

Conditions may use `not`, `and`, `or`, and parentheses. Ordered comparisons and
`~=` are unsupported; use primary search text for fuzzy discovery.

`widget <name>` resolves the exact UObject name. `widget@id` resolves stable
identity. Both return the shortest ancestor chain, then the target's readable
native state and relationships. Exact Widget and Palette Entry reads may use
`with schema`; collections, `tree`, and `summary` may not.

## Object Relationships

Every Widget uses `widget(id, type, nativeFields...)`; `type` is the complete
native UE Class Path. Panel placement nests its native Slot on the child:

```lgl
stack.start = widget(
  id: "start-guid",
  type: "/Script/UMG.Button",
  Slot: {
    type: "/Script/UMG.VerticalBoxSlot",
    Padding: "<FMargin native text>"
  }
)
```

A host or the Blueprint exposes Named Slot relationships through
`NamedSlots: {Header: widget@id, Body: null}`. Panel Slot and Named Slot are not
independent objects and have no query, id, constructor, or Palette entry.

## Palette And Patch

The combined target Palette owns all direct creation. Its Widget entries return
an exact copyable `widget(...)` constructor:

```lgl
query menu
palette @palette-entry-id
with schema
```

The result supplies a binding such as:

```lgl
start = widget(palette: "palette-entry-id")
```

After declaring Palette-backed local bindings such as `root`, `child`,
`wrapper`, or `replacement`, Widget Patch supports:

```lgl
add root
add child to widget@panel-id
add child before widget@anchor-id
add child after widget@anchor-id
add child to widget@host-id.NamedSlots.Header
add child to menu.NamedSlots.Body

set widget@id.NativeField = value
reset widget@id.NativeField
set widget@id.Slot.NativeField = value
reset widget@id.Slot.NativeField

move widget@id to widget@panel-id
move widget@id before widget@anchor-id
move widget@id after widget@anchor-id
move widget@id to widget@host-id.NamedSlots.Header
move widget@id to menu.NamedSlots.Body
remove widget@id

wrap widget@id with wrapper
wrap [widget@id, widget@id] with wrapper
replace widget@id with replacement

invoke widget@id Rename(displayName: "New Name")
invoke widget@id Duplicate() as copy
```

`add` materializes one Palette-backed binding. `wrap` and Palette-backed
`replace` materialize their bindings themselves; do not also `add` them.
`remove` deletes the authored subtree, never detaches it. `NamedSlots` is
read-only relationship state; structural operations own placement. Exact
Widget schema is authoritative for fields, Slot fields, constraints, and
available Operations.

## Events And Finalization

Multicast delegates such as `OnClicked` are Graph-event capabilities. Exact
Widget schema returns locator-complete Graph guidance ending in:

```lgl
palette entries "OnClicked"
where widget = widget@button-guid
```

Widget defines no Event object or event mutation; Graph owns the resulting
Node. Compile and save through a separate Blueprint terminal Patch:

```lgl
patch menu
compile
save
```

Do not mix finalization with Widget edits. Widget Animation, Widget Navigation,
legacy Binding, and MVVM are outside this interface.
