# Widget Module

## Scope

The widget module describes UMG WidgetBlueprint trees in LGL. It is the module
closest to OpenUI-style component construction because widget trees are
naturally hierarchical and component-like.

This module is not implemented yet. It records the target module shape. Exact
constructor names, slot properties, event bindings, and normalized JSON must be
designed against UE UMG semantics and the existing widget tools before
implementation.

## Basic Form

Widget tree text is a statement list:

```lgl
menuAsset = asset(path: "/Game/UI/WBP_Menu.WBP_Menu", type: widget)
menu = widget(asset: menuAsset, root: mainCanvas)

mainCanvas = CanvasPanel()
mainCanvas.stack = VerticalBox()
stack.title = TextBlock(text: "Main Menu", fontSize: 32)
stack.start = Button(text: "Start")
stack.quit = Button(text: "Quit")
```

The widget document declares the root alias with `root: mainCanvas`. The root
widget itself is a normal binding; it does not have to be named `root`.

Hierarchy is expressed by member binding targets:

```lgl
parent.child = WidgetType(...)
```

The parent is the left-side prefix. The child alias is the left-side member.
Sibling order is the order of child binding lines for the same parent.

Structural `{}` blocks are not used.

## Widget Objects

| Object | Syntax | Example |
| --- | --- | --- |
| Widget asset | `name = asset(path: "...", type: widget)` | `menuAsset = asset(path: "/Game/UI/WBP_Menu.WBP_Menu", type: widget)` |
| Widget document | `name = widget(asset: ref, root: ref)` | `menu = widget(asset: menuAsset, root: mainCanvas)` |
| Root widget | `name = WidgetType(props...)` | `mainCanvas = CanvasPanel()` |
| Child widget | `parent.child = WidgetType(props...)` | `stack.start = Button(text: "Start")` |
| Slot metadata | `slot: value` | `stack.start = Button(text: "Start", slot: fill)` |

Widget asset identity uses the asset module and named arguments. Avoid
positional asset constructors:

```lgl
widget("/Game/UI/WBP_Menu.WBP_Menu")
```

Prefer:

```lgl
menuAsset = asset(path: "/Game/UI/WBP_Menu.WBP_Menu", type: widget)
menu = widget(asset: menuAsset, root: mainCanvas)
```

## Constructors

Widget constructors use named arguments:

```lgl
title = TextBlock(text: "Main Menu", fontSize: 32)
start = Button(text: "Start")
mainCanvas = CanvasPanel()
```

Do not use positional widget constructors:

```lgl
Button("Start")
VerticalBox([title, start])
```

Named arguments cost a few more tokens but avoid schema-order guessing:

```lgl
Button(text: "Start")
VerticalBox()
```

## Tree Sugar

Child bindings are widget tree sugar:

```lgl
mainCanvas.stack = VerticalBox()
stack.title = TextBlock(text: "Main Menu", fontSize: 32)
stack.start = Button(text: "Start")
stack.quit = Button(text: "Quit")
```

This normalizes to canonical widget node text with explicit `parent` and
`order` fields:

```lgl
stack = VerticalBox(parent: mainCanvas, order: 0)
title = TextBlock(parent: stack, order: 0, text: "Main Menu", fontSize: 32)
start = Button(parent: stack, order: 1, text: "Start")
quit = Button(parent: stack, order: 2, text: "Quit")
```

Rules:

1. `parent.child = WidgetType(...)` defines `child` as the alias.
2. The parent must be a previously declared widget alias.
3. Sibling order is the order of child binding lines for that parent.
4. Duplicate child aliases under the same document are invalid.
5. The root widget is declared by `widget(..., root: alias)` and may use any
   alias name.

Canonical text uses `parent` and `order`. Normalized JSON should not need a
separate `children` array; child lists can be derived from `parent + order`.

## Tree Readback

Widget tree query results should prefer editor-order tree sugar:

```lgl
menuAsset = asset(path: "/Game/UI/WBP_Menu.WBP_Menu", type: widget)
menu = widget(asset: menuAsset, root: mainCanvas)

mainCanvas = CanvasPanel()
mainCanvas.stack = VerticalBox()
stack.title = TextBlock(text: "Main Menu", fontSize: 32)
stack.start = Button(text: "Start")
stack.quit = Button(text: "Quit")
```

This format is flat, streamable, and close to the hierarchy shown in the UMG
editor. Agents can read a line such as `stack.start = ...` without scanning
the root tree to know the widget's parent.

## Slots

Slot data belongs to the relationship between a parent container and a child.
The exact syntax must follow UMG slot semantics.

Simple slot metadata may appear on the child constructor:

```lgl
stack.start = Button(text: "Start", slot: fill)
```

Richer slot data should use an inline object value:

```lgl
mainCanvas.stack = VerticalBox(slot: {anchors: fill, padding: [16, 12, 16, 12]})
```

Slot sugar must normalize to explicit slot data attached to the child node
before normalized JSON.

## Query

Widget queries use the shared query shape:

```lgl
query menu
find widgets
where type = Button and text ~= "Start"
with slots, events
order by name asc
limit 10
```

Supported first-pass `find` forms should include:

- `find tree`
- `find widgets`
- `find widget name`
- `find children of name`
- `find ancestors of name`

The default widget query result should include widget identity, class/type,
name, parent, and order. `find tree` should return the tree in editor order.
`with slots` expands slot metadata. `with events` expands event bindings.

## Patch

Widget patch text is a statement list:

```lgl
patch menu dry run

stack.help = Button(text: "Help")
add help
set title.text = "Main Menu"
move help after start
remove quit
```

Patch operation names should stay close to UE widget tree operations:

| Operation | Syntax | Example |
| --- | --- | --- |
| Add widget | `parent.child = WidgetType(...); add child` | `stack.help = Button(text: "Help")` |
| Set property | `set target = value` | `set title.text = "Main Menu"` |
| Move widget | `move child before/after sibling` | `move help after start` |
| Remove widget | `remove name` | `remove quit` |

The exact operation set must be designed against the existing widget tool
schema and UMG WidgetTree APIs.

## Normalized JSON

The exact schema should be added when the widget module is implemented. The
target shape should normalize tree sugar to explicit parent and order fields:

```ts
interface WidgetDocument {
  kind: "widget";
  target: {
    asset: string;
  };
  root: string;
  widgets: WidgetNode[];
}

interface WidgetNode {
  alias: string;
  class: string;
  parent?: string | null;
  order?: number;
  properties: Record<string, Value>;
  slot?: WidgetSlot;
}
```

This is illustrative only. The real schema should be based on UMG widget tree
tools and UE widget semantics.

## Adapter Boundary

Pure LGL normalization may:

- convert `parent.child = WidgetType(...)` sugar into explicit `parent` and
  `order` fields
- derive sibling order from source order within the same parent
- convert slot sugar into explicit slot data
- normalize widget query clauses into a structured query object

Pure LGL normalization must not:

- load WidgetBlueprint assets
- validate UMG classes
- validate property names or types
- decide whether a widget can be parented under a container
- compile WidgetBlueprints
- bind events to graph functions

The adapter or bridge owns those UE-dependent responsibilities.
