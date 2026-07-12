# Widget Domain

## Scope

The widget domain describes UMG WidgetBlueprint trees in LGL. It is the domain
closest to OpenUI-style component construction because widget trees are
naturally hierarchical and component-like.

The TypeScript LGL experiment implements widget object readback, widget query
normalization, patch normalization, formatting, schema validation, and an
in-memory widget adapter. The UE-backed adapter routes the same object model
through UMG WidgetTree APIs.

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

Widget asset identity uses the asset domain and named arguments. Widget
constructors also use named arguments. Avoid positional forms:

```lgl
widget("/Game/UI/WBP_Menu.WBP_Menu")
Button("Start")
VerticalBox([title, start])
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

Sibling order is derived from statement order for each parent. LGL text does
not require agents to write explicit order fields.

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

Widget query results should prefer editor-order tree sugar. The format is
flat, streamable, and close to the hierarchy shown in the UMG editor.

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
widgets "Start"
where type = Button
order by name asc
page limit 10
```

Widget query syntax:

```lgl
query <widget>
tree
widgets ["text"]
palette entries ["text"]
where <condition>
with <item>, <item>
order by <key> asc|desc, <key> asc|desc
page limit <number>
page after "cursor"
```

For `widgets`, the quoted text is the primary search text over widget name,
class/type, and relevant display text. Use `where` for structured filters such
as `type = Button`.

For `palette entries`, the quoted text is the primary search text over
entry name, class path, palette id, label, and category. It returns creation
entries that can be copied directly into patch text. Use `with defaults` to
include common creation arguments and `with properties` to include writable
property metadata.

Exact widget lookup and parent-child queries use structured filters:

```lgl
widgets
where name = start

widgets
where parent = stack
```

The default widget query result includes widget identity, class/type, name, and
parent. `tree` returns the tree in editor order. Slot and event expansion
are separate query expansions outside the TypeScript adapter surface.

The normalized JSON representation of `tree`, `widgets`, and `palette entries`
is not specified yet. It must be reviewed with the shared query contract rather
than silently reusing the experiment's earlier `find = Find` field. The widget
domain still owns allowed fields, expansions, sort keys, and pagination
defaults. The current TypeScript experiment has not migrated to this text
design.

## Palette

Widget palette queries discover creation forms for widget patching. They use
the shared `palette entries` operation, but results prefer the
most direct creation text rather than forcing every entry through a palette id.

```lgl
query menu
palette entries "Button"
with defaults
page limit 10
```

Default results stay compact:

```lgl
Button = Button()
TextBlock = TextBlock()
```

`with defaults` includes common writable creation arguments that are useful
immediately in patch text:

```lgl
Button = Button(text: "")
TextBlock = TextBlock(text: "", fontSize: 24)
```

`with properties` asks the adapter to include writable property metadata for
the returned creation entries. Property metadata is descriptive; patch text
still writes properties as constructor arguments or `set widget.property = ...`.

```lgl
Button.text = property(type: string, default: "", writable: true)
```

`with defaults, properties` may be combined:

```lgl
Button = Button(text: "")
Button.text = property(type: string, default: "", writable: true)
```

Use a stable widget constructor when the entry maps to a common native UMG
widget with clear semantics:

```lgl
Button = Button()
CanvasPanel = CanvasPanel()
VerticalBox = VerticalBox()
```

Use `widget(class: "...")` for project widgets, user widgets, and other entries
where a UE widget class path is the complete creation identity:

```lgl
InventorySlot = widget(class: "/Game/UI/WBP_InventorySlot.WBP_InventorySlot_C")
```

Use `widget(palette: "...")` only as a fallback for plugin or template entries
that cannot be represented safely as a constructor or class path:

```lgl
PluginFancy = widget(palette: "widget.palette:...")
```

Patch text consumes the returned creation form directly:

```lgl
patch menu

add mainCanvas.stack = VerticalBox()
add stack.title = TextBlock(text: "Main Menu")
add stack.start = Button(text: "Start")
add stack.slot = widget(class: "/Game/UI/WBP_InventorySlot.WBP_InventorySlot_C")
add stack.fancy = widget(palette: "widget.palette:...")
```

Widget palette is class-driven. Common native widgets return constructor
entries such as `Button()` and `TextBlock()`. User widgets and WidgetBlueprint
assets return `widget(class: "...")`. Special templates that cannot be
represented by constructor or class path return `widget(palette: "...")`.

The UE-backed adapter should model the UMG editor palette, including native
`UWidget` classes, user widget classes, unloaded WidgetBlueprint assets from
the Asset Registry, hidden/deprecated/editor-only filtering, and project
palette settings.

`with defaults` is intentionally small. It should return common writable
creation arguments that make immediate patch authoring easier. `with
properties` returns property metadata for discoverability. Neither expansion
should expose events or slot schema; those belong to separate `with events` and
`with slots` expansions.

The adapter must validate palette entries against UE state before returning or
executing them:

1. The class or template resolves for the target WidgetBlueprint context.
2. The class is not abstract, deprecated, hidden, or editor-only when the target
   WidgetBlueprint disallows editor widgets.
3. The class is not the same WidgetBlueprint class currently being edited.
4. Asset Registry entries that are not loaded still provide enough metadata to
   produce a stable `widget(class: "...")` result, or they fall back to
   `widget(palette: "...")`.
5. Parent compatibility and slot validity are checked during patch validation
   and apply, because they depend on the actual `add parent.child = ...`
   target.

Normalized JSON:

```ts
interface PaletteResult {
  kind: "palette_result";
  target: Target;
  entries: CreationEntry[];
}

type CreationEntry =
  | ShortcutEntry
  | ClassEntry
  | PaletteEntry;

interface ShortcutEntry {
  name: string;
  constructor: Call;
  defaults?: Record<string, Expr>;
  properties?: Property[];
}

interface ClassEntry {
  name: string;
  class: string;
  label?: string;
  category?: string;
  defaults?: Record<string, Expr>;
  properties?: Property[];
}

interface PaletteEntry {
  name: string;
  palette: PaletteSourceRef;
  label?: string;
  category?: string;
  defaults?: Record<string, Expr>;
  properties?: Property[];
}

interface Property {
  name: string;
  type: string;
  default?: Expr;
  writable?: boolean;
  category?: string;
}
```

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
| Add widget | `add parent.child` or `add parent.child = WidgetType(...)` | `add stack.help = Button(text: "Help")` |
| Set property | `set target = value` | `set title.text = "Main Menu"` |
| Move widget | `move child before/after sibling` | `move help after start` |
| Remove widget | `remove name` | `remove quit` |

`add parent.child = WidgetType(...)` uses the shared patch sugar from the
language core. Its canonical form is a child widget binding followed by
`add parent.child`:

```lgl
stack.help = Button(text: "Help")
add stack.help
```

The TypeScript LGL experiment implements `add`, `set`, `move`, and `remove` in the
in-memory widget adapter. The UE-backed adapter must route the same operations
through UMG WidgetTree APIs and slot/property edit paths.

Normalized JSON:

```ts
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
  target: FieldPath;
  relativeTo: FieldPath;
  position: "before" | "after";
}

interface Remove {
  kind: "remove";
  target: FieldPath;
}
```

Widget patch text uses the shared `Patch` envelope with `target.domain =
"widget"` and `ops = PatchOp[]`.

The adapter resolves targets to WidgetTree instances and applies operations
through UMG WidgetTree APIs.

## Adapter Boundary

Pure LGL normalization may:

- convert `parent.child = WidgetType(...)` sugar into explicit `parent` while
  preserving widget statement order
- convert slot sugar into explicit slot data
- normalize widget query clauses into a structured query object
- normalize widget palette creation bindings into constructor, class, or
  palette creation records

Pure LGL normalization must not:

- load WidgetBlueprint assets
- validate UMG classes
- validate property names or types
- decide whether a widget can be parented under a container
- compile WidgetBlueprints
- bind events to graph functions

The adapter or bridge owns those UE-dependent responsibilities.
