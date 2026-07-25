# Widget Domain

## Scope

Widget Domain exposes authored `UWidget` objects inside one
`UWidgetBlueprint::WidgetTree`:

- Widget tree and detached source Widgets;
- native Widget and Panel Slot state;
- Named Slot relationships;
- Widget Palette;
- structural add, move, wrap, replace, remove, rename, and duplicate;
- handoffs to Graph events and Blueprint finalization.

Widget Animations, Navigation, legacy `UWidgetBlueprint::Bindings`, and MVVM
remain outside the current Domain.

Those objects are not directly queryable or editable through Widget Domain,
but native Widget operations may affect their references. Every determinable
Animation/MovieScene, Navigation, Binding, Graph, generated-Class, and
Extension cascade remains part of preflight isolation, the mutation plan, and
result effects.

## Target

```sal
menu = target {
  domain: widget,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "11111111-1111-1111-1111-111111111111"
}
```

Discovery Query may omit `id`; canonical exact readback and every Patch include
verified `BlueprintGuid`.

The same native `UWidgetBlueprint` can have a separate Blueprint Target:

```sal
menuBlueprint = target {
  domain: blueprint,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "11111111-1111-1111-1111-111111111111"
}
```

These Targets do not combine Query, identity, Palette, or mutation surfaces.
Widget Domain is selected only by `domain: widget`, never by native Class.

## Identity

Each source Widget uses its entry in
`UWidgetBlueprint::WidgetVariableNameToGuidMap`:

```sal
@widget-guid
```

The Guid is Target-relative. UE preserves it through Widget rename. A missing
or duplicate map entry is invalid source state; Query never creates one.

Object name and `DisplayLabel` are discovery data, not identity. Panel Slot and
Named Slot relationships have no independent StableRef.

## Object Text

Widget state is ordinary brace data:

```sal
mainCanvas = {
  id: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  type: "/Script/UMG.CanvasPanel"
}

mainCanvas.stack = {
  id: "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb",
  type: "/Script/UMG.VerticalBox",
  Slot: {
    type: "/Script/UMG.CanvasPanelSlot",
    LayoutData: "<FAnchorData native text>"
  }
}

stack.title = {
  id: "cccccccc-cccc-cccc-cccc-cccccccccccc",
  type: "/Script/UMG.TextBlock",
  Text: "<FText native text>",
  Slot: {
    type: "/Script/UMG.VerticalBoxSlot",
    Padding: "<FMargin native text>"
  }
}
```

`widget` is a reserved Domain keyword and is not a semantic tag. Alias and
owner path provide reading structure; `id`, `type`, and native fields provide
data.

Native Classes are never object constructors. Native and generated Classes
retain complete UE Paths.

## Panel Slots

A `UPanelSlot` is native placement/layout state owned by a child Widget. It is
nested under `Slot`, not returned as an object:

```sal
stack.start = {
  id: "dddddddd-dddd-dddd-dddd-dddddddddddd",
  type: "/Script/UMG.Button",
  Slot: {
    type: "/Script/UMG.VerticalBoxSlot",
    Padding: "<FMargin native text>",
    HorizontalAlignment: HAlign_Fill
  }
}
```

`Parent` and `Content` are omitted because the binding path expresses the
relationship. Root and Named Slot content have no Panel `Slot`.

Slot fields use Widget member paths:

```sal
set @start-guid.Slot.Padding = "<FMargin native text>"
reset @start-guid.Slot.Padding
```

There is no Slot query, StableRef, Palette entry, or lifecycle.

## Named Slots

`INamedSlotInterface` relationships appear as a map on their native host:

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

Inherited WidgetTree-level Named Slot content uses the Target alias:

```sal
menu.NamedSlots = {
  Header: @header-guid,
  Body: null
}
```

Named Slots are relationships, not objects. Keys outside member-path grammar
remain readable through quoted object keys and exact comments; mutation is
unavailable until the path can be represented losslessly.

Reading order:

1. host `NamedSlots`;
2. Named Slot content;
3. ordinary Panel children in native order.

A source Widget matching no root, Panel, or Named Slot relationship is
detached/corrupt and is reported rather than attached by guess.

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

`target with schema` reads Widget Domain Target capabilities. `summary`
returns the root identity and source/reachable/detached counts.

`tree` returns structure and Slot layout, defaults to depth 20, and marks
truncated boundaries. A nested User Widget is one leaf; its internal tree
belongs to another Widget Target.

`widgets` enumerates every source Widget, including detached objects. Search
covers native object/display names and Class identity. Exact filters:

| Field | Operators |
| --- | --- |
| `name`, `id`, `type`, `DisplayLabel` | `=`, `!=` |
| `bIsVariable`, `reachable` | `=`, `!=`, boolean shorthand |

Predicates may combine with `not`, `and`, `or`, and parentheses. Ordered
comparisons and `~=` are rejected. Ordering keys are `name`, `type`, and `id`,
each ascending or descending. Pagination uses the shared cursor model and
defaults to 50.

`widget <name>` is exact-name discovery. StableRef is exact identity. Exact
reads return the shortest ancestor chain plus full target Widget state and may
use `with schema`.

References are local to the WidgetBlueprint by default. `in project` uses the
bounded shared provider. Widget binding use-sites retain native field/path
evidence instead of becoming a Binding object.

The Query clause surface is closed:

| Operation | Filters | Ordering | Pagination | Schema |
| --- | --- | --- | --- | --- |
| `target` | none | none | none | optional |
| `summary` | none | none | none | none |
| `tree` | optional root and `depth` only | none | none | none |
| `widgets` | search text and the fields above | `name`, `type`, `id` | cursor, default 50 | none |
| `widget <name>` / `@widget-guid` | none | none | none | optional |
| `references` | shared reference clauses only | shared contract | shared contract | none |
| `palette entries` | search text only | none | cursor, default 50 | none |
| `palette @id` | none | none | none | optional |

In particular, Palette collection queries accept no `where`, `order by`, or
collection `with schema`; exact Palette entries may use `with schema`.

## Schema And Graph Events

Exact Widget schema covers:

- Widget fields;
- nested Panel Slot fields;
- Named Slot destinations;
- structural operation availability;
- Graph-event delegate guidance.

An accessible multicast delegate such as `OnClicked` is Graph capability, not
writable Widget state. Schema returns an independent Graph Target:

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

The following request searches that Graph Palette:

```sal
query eventGraph
palette entries "OnClicked"
where widget = @button-guid
```

Here the Widget identity is an owning-Blueprint declaration explicitly
supported by Graph Domain's Palette condition. It does not compose Widget
Domain into Graph.

If an event Node already exists, the Widget result returns the Graph Target and
a scoped Node reference such as `eventGraph::@event-node-guid`.

A Widget is eligible only when its declaration resolves to a real
`FObjectProperty` in the generated or Skeleton Class. When mapping a stored
`FMemberReference` back to a Widget declaration, resolution must match that
native Property identity; the adapter never falls back to Widget name or
`bIsVariable`. An inherited `BindWidget` keeps the identity of its actual
declaring Class Property rather than being rewritten as the current Widget
Guid.

If multiple Ubergraphs can own the event, schema returns one complete related
Graph Target and handoff for every compatible Graph. It never chooses one from
editor focus or `GetLastEditedUberGraph()`.

A Widget without that generated object Property may require:

```sal
set @button-guid.bIsVariable = true
```

This follows Widget Designer structural modification behavior; it is not a raw
Boolean memory write. The Boolean alone is never accepted as proof that the
required generated Property already exists.

## Palette

Widget Domain owns one Widget Palette:

```sal
query menu
palette entries "Button"

query menu
palette @P_Button
with schema
```

Results are ordinary ObjectExpr:

```sal
Button = { palette: "P_Button" }
```

The opaque id selects an exact `FWidgetTemplate`. It is not a future Widget id,
native Class constructor, or Domain selector.

The adapter revalidates:

- template availability;
- native Class usability;
- circular User Widget references;
- exact requested name uniqueness;
- structural destination/capacity.

`FWidgetTemplate::Create` may create a native subtree. The binding names the
primary Widget; descendants are native effects and receive real ids in final
Object Text.

## Patch

Patch requires the canonical Widget Target. Creation bindings and operations
are ordered separately.

### Add

```sal
patch menu

root = { palette: "P_CanvasPanel" }
add root

start = { palette: "P_Button" }
add start to @stack-guid

header = { palette: "P_TextBlock" }
add header to @area-guid.NamedSlots.Header
```

Forms:

```sal
add root
add child to @panel-guid
add child before @anchor-guid
add child after @anchor-guid
add child to @host-guid.NamedSlots.Header
add child to menu.NamedSlots.Body
```

Bare `add` creates the root only when none exists. Panel placement creates the
native Slot. Named Slot placement requires an existing empty exact slot and
never replaces content implicitly.

### Set, Move, Remove

```sal
set @title-guid.Text = "<FText native text>"
reset @title-guid.Text

move @help-guid after @start-guid
move @help-guid to @stack-guid
move @title-guid to @area-guid.NamedSlots.Header

remove @quit-guid
```

`set` and `reset` use exact instance schema and never change structural
ownership. `NamedSlots` is read-only relationship state.

Widget and Panel Slot fields are writable only when the exact native instance
passes the same predicates used by schema: `EditConst`,
`DisableEditOnTemplate`, `BlueprintReadOnly`, or a failed `CanEditChange`
rejects the edit. `reset` additionally rejects `NoResetToDefault`.
`DisableEditOnInstance` alone does not prohibit editing an authored template.

Move preserves same-Panel Slot when reordering. Reparenting creates a new
native Slot and imports only compatible old Slot values. Moving to/from Named
Slots clears and creates the correct relationship without fabricating a Panel
Slot.

`remove` means native subtree deletion, never detach. It removes the authored
subtree, clears each removed Widget GUID record and applicable Graph variable
node, removes delegate bindings owned by the selected deletion root, clears
Desired Focus when it names that root, and structurally modifies the
WidgetBlueprint. This is the exact UE 5.7 `DeleteWidgets` cascade; SAL does not
claim extra descendant-binding cleanup that the native path does not perform.
Every determinable cascade is planned.

### Wrap

```sal
wrapper = { palette: "P_VerticalBox" }
wrap @title-guid with wrapper

wrapper = { palette: "P_VerticalBox" }
wrap [@title-guid, @body-guid] with wrapper
```

`wrap` materializes its binding; separate `add wrapper` is invalid. Multiple
targets must be direct siblings under one Panel and are inserted into the
wrapper in explicit array order. Root and Named Slot content can be wrapped
only alone.

The Palette capability must create exactly one primary `UPanelWidget`, and that
Panel must have capacity for every target. A multi-target selection may not
contain duplicates or an ancestor/descendant pair. Detached Widgets are
rejected rather than silently attached or ignored.

The wrapper takes the first target's external Root/Panel/Named Slot
relationship. Every internal Slot creation and discarded old Slot is planned.

### Replace

```sal
replacement = { palette: "P_Border" }
replace @old-guid with replacement

replace @panel-guid with @only-child-guid
```

Palette-backed replace follows Widget Designer Replace With, preserves the
logical Widget Guid through the native replacement path, transfers external
placement, imports compatible properties, and moves compatible children.

Existing-content promotion is valid only for the target's only direct Panel
child or direct Named Slot content. It is not a generic move-and-delete.

### Schema Operations

```sal
invoke @start-guid Rename(displayName: "Start Button")
invoke @start-guid Duplicate() as copy
```

Rename follows the complete Widget editor path and preserves Widget Guid while
updating native references. Duplicate uses native subtree serialization,
creates new names and Guids, and is available only with an unambiguous Panel
sibling destination.

Clipboard Cut/Copy/Paste and lifecycle aliases such as attach/detach/reparent
are not Patch operations.

## Dry Run And Transactions

Dry run executes the same ordered native edits against a fully isolated
WidgetBlueprint sandbox, including WidgetTree, Slots, Graphs, generated
Classes/CDOs, Animations/MovieScenes, Bindings, Navigation, and Extensions.

If the adapter cannot prove all writable objects are isolated, preflight fails.
Creation bindings must be consumed exactly once by add, wrap, or replace.
Transient Guids never escape.

Live apply uses one transaction. Every relationship change and native cascade
appears in ordered `planned.effects`.

## Blueprint Finalization Handoff

Widget Domain does not compile or save the WidgetBlueprint:

The following is a Result Text fragment, not a standalone Result Text document.

```sal
related bp = target {
  domain: blueprint,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "11111111-1111-1111-1111-111111111111"
}
handoff compile to bp
```

Compile/save occurs in a following Blueprint request. Widget and Blueprint
authored edits never share one Patch.

## UE Boundary

The adapter follows native ownership and editor paths:

- `UBaseWidgetBlueprint::WidgetTree`;
- `UWidgetTree::RootWidget`;
- `ForEachSourceWidget` and `ForWidgetAndChildren`;
- `UPanelSlot` and `INamedSlotInterface`;
- `FWidgetTemplate::Create`;
- Widget GUID maintenance;
- Designer hierarchy drop;
- native wrap, replace, rename, duplicate, and delete utilities.

It never guesses a Class from display text, raw-constructs in place of an exact
template, silently suffixes names, synthesizes missing ids on read, or collapses
Panel and Named Slot relationships.
