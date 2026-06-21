# Widget Domain

## Scope

The widget domain describes UMG WidgetBlueprint trees in LGL. It is the domain
closest to OpenUI-style component construction because widget trees are
naturally hierarchical and component-like.

The TypeScript LGL experiment implements widget object readback, widget query
normalization, formatting, schema validation, and an in-memory query adapter.
Widget patching and the UE-backed adapter still need to be designed against UMG
WidgetTree APIs before bridge implementation.

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

Widget asset identity uses the asset domain and named arguments. Avoid
positional asset constructors:

```lgl
widget("/Game/UI/WBP_Menu.WBP_Menu")
```

Prefer:

```lgl
menuAsset = asset(path: "/Game/UI/WBP_Menu.WBP_Menu", type: widget)
menu = widget(asset: menuAsset, root: mainCanvas)
```

Normalized JSON:

```ts
interface Document {
  alias: string;
  asset: string;
  root: string;
  widgets: Node[];
}

interface Node {
  alias: string;
  class: string;
  parent?: string;
  properties?: Record<string, Value>;
}
```

Sibling order is derived from statement order for each parent. A future schema
may store explicit order for bridge convenience, but LGL text should not require
agents to write it.

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

This normalizes to canonical widget node text with explicit `parent`. Sibling
order remains the order of widget statements in text:

```lgl
stack = VerticalBox(parent: mainCanvas)
title = TextBlock(parent: stack, text: "Main Menu", fontSize: 32)
start = Button(parent: stack, text: "Start")
quit = Button(parent: stack, text: "Quit")
```

Rules:

1. `parent.child = WidgetType(...)` defines `child` as the alias.
2. The parent must be a previously declared widget alias.
3. Sibling order is the order of child binding lines for that parent.
4. Duplicate child aliases under the same document are invalid.
5. The root widget is declared by `widget(..., root: alias)` and may use any
   alias name.

Canonical text uses `parent` and preserves statement order. Normalized JSON
should not need a separate `children` array; child lists can be derived from
parent plus order.

## Tree Object Text

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

Normalized JSON:

```ts
interface Slot {
  parent: string;
  child: string;
  properties: Record<string, Value>;
}
```

Slot data is relationship metadata. It is stored on the child node for compact
transport, but the adapter must apply it through the UMG slot object owned by
the parent-child relationship.

## Query

Widget queries use the shared query shape:

```lgl
query menu
find widgets "Start"
where type = Button
order by name asc
page limit 10
```

Widget query syntax:

```lgl
query <widget>
find tree
find widgets ["text"]
where <condition>
with <item>, <item>
order by <key> asc|desc, <key> asc|desc
page limit <number>
page after "cursor"
```

For `find widgets`, the quoted text is the primary search text over widget name,
class/type, and relevant display text. Use `where` for structured filters such
as `type = Button`.

Exact widget lookup and parent-child queries use structured filters:

```lgl
find widgets
where name = start

find widgets
where parent = stack
```

The default widget query result includes widget identity, class/type, name, and
parent. `find tree` returns the tree in editor order. Slot and event expansion
are future UE-backed adapter work, not part of the current TypeScript adapter.

Normalized JSON:

```ts
type WidgetQuery = Query<Find>;

type Find =
  | FindTree
  | FindWidgets;

interface FindTree {
  kind: "tree";
}

interface FindWidgets {
  kind: "widgets";
  text?: string;
}
```

`where`, `with`, `orderBy`, and `page` use the shared query model from the
language core. The widget domain validates allowed fields, expansions, sort
keys, and pagination defaults.

## Patch

Widget patch text is a statement list:

```lgl
patch menu dry run

add stack.help = Button(text: "Help")
set title.text = "Main Menu"
move help after start
remove quit
```

Patch operation names should stay close to UE widget tree operations:

| Operation | Syntax | Example |
| --- | --- | --- |
| Add widget | `add parent.child = WidgetType(...)` | `add stack.help = Button(text: "Help")` |
| Set property | `set target = value` | `set title.text = "Main Menu"` |
| Move widget | `move child before/after sibling` | `move help after start` |
| Remove widget | `remove name` | `remove quit` |

`add parent.child = WidgetType(...)` uses the shared patch sugar from the
language core. Its canonical form is a child widget binding followed by
`add child`.

The TypeScript LGL experiment implements `add`, `set`, and `remove` in the
in-memory widget adapter. The UE-backed adapter must route the same operations
through UMG WidgetTree APIs and slot/property edit paths.

Normalized JSON:

```ts
type WidgetPatch = Patch<PatchOp>;

type PatchOp =
  | Add
  | Set
  | Move
  | Remove;

interface Add {
  kind: "add";
  target: FieldPath;
}

interface Set {
  kind: "set";
  target: FieldPath;
  value: Expr;
}

interface Move {
  kind: "move";
  target: Ref;
  relativeTo: Ref;
  position: "before" | "after";
}

interface Remove {
  kind: "remove";
  target: FieldPath;
}
```

The adapter resolves targets to WidgetTree instances and applies operations
through UMG WidgetTree APIs.

## Normalized JSON

Widget normalized JSON is defined beside each feature above. The summary below
shows the top-level widget-domain payloads:

```ts
interface WidgetResult {
  kind: "widget_result";
  documents: WidgetDocument[];
}

// Widget object text
WidgetDocument

// Widget query and patch text
type WidgetQuery = Query<Find>;
type WidgetPatch = Patch<PatchOp>;
```

The current schema covers readback and query. Patch JSON must be added only
after the widget mutation design is fixed against UMG WidgetTree tools and UE
widget semantics.

## Adapter Boundary

Pure LGL normalization may:

- convert `parent.child = WidgetType(...)` sugar into explicit `parent` while
  preserving widget statement order
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
