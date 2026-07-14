# Widget Domain

## Scope

The Widget domain describes the authored `UWidget` objects inside a
`UWidgetBlueprint::WidgetTree`. It does not introduce a parallel Widget
document object or translate Widget Classes and properties into LGL-specific
types.

This document is the normative LGL design. The current TypeScript experiment
and UE-backed Widget adapter predate it and remain implementation gaps. Bridge
work is intentionally deferred until the LGL documents are complete.

## UE Object Boundary

The domain maps UE objects directly:

| UE concept | LGL representation |
| --- | --- |
| `UWidgetBlueprint` | the existing `blueprint(...)` object shape |
| owned `UWidgetTree` | the Blueprint's tree relationship, not another object binding |
| one authored `UWidget` | `widget(...)` |
| `FWidgetTemplate` | one Palette creation capability |

`UWidgetBlueprint` already owns its `UWidgetTree`. LGL therefore has no
`widget(asset: ..., root: ...)` wrapper. The `tree` query is a view of that
owned relationship, not a constructor or result object.

Every authored Widget uses the same neutral object shape. Native Classes do not
become constructors such as `Button()`, `CanvasPanel()`, or `TextBlock()`.
Existing objects return the exact UE Class path in `type`, and native properties
retain their exact UE names and values.

## Object Text

Widget Object Text begins with its Asset and Blueprint context, then emits
ordinary `widget(...)` bindings in a useful reading order:

```lgl
menuAsset = asset(
  path: "/Game/UI/WBP_Menu.WBP_Menu",
  type: "/Script/UMGEditor.WidgetBlueprint"
)

menu = blueprint(
  asset: menuAsset,
  id: "blueprint-guid",
  type: BPTYPE_Normal,
  ParentClass: "/Script/UMG.UserWidget"
)

mainCanvas = widget(
  id: "canvas-guid",
  type: "/Script/UMG.CanvasPanel"
)

mainCanvas.stack = widget(
  id: "stack-guid",
  type: "/Script/UMG.VerticalBox",
  Slot: {
    type: "/Script/UMG.CanvasPanelSlot",
    LayoutData: "<FAnchorData native text>"
  }
)

stack.title = widget(
  id: "title-guid",
  type: "/Script/UMG.TextBlock",
  Text: "<FText native text>",
  Slot: {
    type: "/Script/UMG.VerticalBoxSlot",
    Padding: "<FMargin native text>",
    HorizontalAlignment: HAlign_Fill
  }
)
```

The unique local top-level Widget binding in a tree is
`UWidgetTree::RootWidget`. It needs no duplicate `root` argument. An empty
WidgetTree has no root Widget binding.

A source Widget's `id` maps to its entry in
`UWidgetBlueprint::WidgetVariableNameToGuidMap`. UE preserves that GUID through
`OnVariableRenamed`, so later requests use `widget@id` rather than a current
name. A missing or duplicate entry is invalid source state and is reported; a
read never creates one. Query-result aliases remain document-local.

Every `type` value is the complete native UE identity. For a native C++ Widget,
`/Script/UMG.Button` means package `/Script/UMG` plus Class `Button`; it is not a
filesystem address. LGL does not shorten it to `UMG.Button` or add an implicit
`/Script` rule. WidgetBlueprint-generated Classes and plugin Classes likewise
retain their exact UE paths.

Native Widget properties are flattened onto the `widget(...)` Call only when
they are part of the requested object state. LGL does not rename `Text` to
`text`, split native `Font` data into `fontSize`, or invent friendly Widget
property types. `with schema` discovers the exact readable, writable, and
resettable fields for the resolved Widget Class.

### Panel Slots

A `UPanelSlot` is relationship and layout state owned by a Widget's placement
inside a `UPanelWidget`. It is nested on the placed Widget under `Slot`; it is
not an independent query object or binding:

```lgl
stack.start = widget(
  id: "start-guid",
  type: "/Script/UMG.Button",
  Slot: {
    type: "/Script/UMG.VerticalBoxSlot",
    Padding: "<FMargin native text>",
    HorizontalAlignment: HAlign_Fill
  }
)
```

`Slot.type` is the exact native `UPanelSlot` Class path. The remaining keys are
the exact readable native Slot properties selected by the query. The adapter
does not repeat `Parent` and `Content`, because the member binding already
expresses that relationship. A root Widget and Named Slot content have no
`Slot` value.

Panel Slot fields use the same native field path for edits:

```lgl
set widget@start-guid.Slot.Padding = "<FMargin native text>"
reset widget@start-guid.Slot.Padding
```

There is no `slot` query, stable Slot id, Slot Palette entry, or Slot
constructor. UE creates and owns the concrete Slot as part of the parent-child
operation.

### Named Slots

Named Slots are a different relationship exposed by `INamedSlotInterface`.
Their content is always a Widget, but the host is not necessarily a
`UNamedSlot` Widget. For example, `UExpandableArea` exposes `Header` and `Body`
directly through the interface.

The host carries one adapter-modeled `NamedSlots` relationship map. Keys are UE
slot names; values are typed stable Widget references or `null`:

```lgl
area = widget(
  id: "area-guid",
  type: "/Script/UMG.ExpandableArea",
  NamedSlots: {
    Header: widget@title-guid,
    Body: null
  }
)

area.title = widget(
  id: "title-guid",
  type: "/Script/UMG.TextBlock"
)
```

`NamedSlots` is not a translated UObject and introduces no Named Slot result
type. It losslessly records the relationship exposed by `GetSlotNames` and
`GetContentForSlot`. A stable reference may point to a Widget binding emitted
later in the same ordered result because it resolves existing target state;
document-local aliases still require an earlier binding.

Inherited Named Slot content stored directly by the current WidgetTree has no
local host Widget. In that case the owning `blueprint(...)` carries the same
relationship map:

```lgl
menu = blueprint(
  asset: menuAsset,
  id: "blueprint-guid",
  type: BPTYPE_Normal,
  NamedSlots: {
    Header: widget@header-guid,
    Body: null
  }
)

menu.headerContent = widget(
  id: "header-guid",
  type: "/Script/UMG.TextBlock"
)
```

A real `UNamedSlot` remains an ordinary
`widget(type: "/Script/UMG.NamedSlot")`. Since it is a `UContentWidget`, its
own content follows the ordinary Panel child relationship and has a native
Panel `Slot`.

Bindings and relationship fields are read together:

- a local Widget without `Slot` is the WidgetTree root;
- `parent.child` with `Slot` is a Panel child;
- `host.child` without `Slot`, whose id appears in `host.NamedSlots`, is Named
  Slot content;
- a `NamedSlots` value of `null` means that the named destination is empty;
- a source Widget matching none of those relationships is detached or corrupt,
  and the adapter reports it rather than guessing.

Sibling order is the order of child binding statements under the same parent.
For a host, Named Slot relationships are emitted before ordinary Panel
children, matching `UWidgetTree::ForWidgetAndChildren`. Structural braces,
`children` arrays, separate Slot bindings, and translated parent constructor
arguments are unnecessary.

Normalized JSON uses the shared ordered `Binding`, ordinary `Call`, recursive
inline objects, and typed references. It has no Widget-specific document,
Panel Slot, or Named Slot expression.

## Query

Widget reads use the shared summary, collection, exact-name, exact-id, and
Palette model:

```lgl
query menu
summary

query menu
tree [widget@<id>] [depth <N>]

query menu
widgets ["text"]

query menu
widget <name>

query menu
widget@<id>

query menu
palette entries ["text"]

query menu
palette @<id>
```

`menu` resolves a `UWidgetBlueprint`, not an artificial Widget document. Each
query has exactly one primary operation. A typed stable reference such as
`widget@id` is itself the exact-id operation and does not take `find`.

### Summary

The `summary` operation is owned by the Widget adapter. It returns compact
Asset and Blueprint context, the root identity when one exists, and ordered
comments for useful counts such as total source Widgets, reachable Widgets,
and detached Widgets. It does not expand the tree or introduce a
summary-specific object.

### Tree

`tree` returns the current WidgetBlueprint's editable authored structure. It is
not a runtime/effective tree and does not automatically expand a parent
WidgetBlueprint. Inherited Named Slot destinations whose content is authored
in the current asset are represented through `blueprint.NamedSlots`; source
comments identify the parent Class so the agent can query it separately.

A nested `UUserWidget` is one leaf in this tree. Its internal WidgetTree belongs
to another WidgetBlueprint target and is never expanded implicitly.

The default is `depth 20`:

```lgl
query menu
tree

query menu
tree widget@stack-guid depth 2
```

The root of the requested view is depth zero. Each Widget-to-Widget structural
hop consumes one level; nested `Slot` state and `NamedSlots` relationship maps
do not. When descendants remain beyond the requested depth, the adapter places
an ordinary comment at each truncated boundary. A local tree operation accepts
one stable Widget reference, not a guessed display label.

The default tree result is a structure-and-layout skeleton:

- Asset and Blueprint context;
- Widget binding, `id`, and exact native `type`;
- `DisplayLabel` only when it carries meaningful authored identity;
- Panel `Slot` layout and `NamedSlots` relationships;
- no arbitrary Widget detail such as `Text`, `Brush`, or `Visibility`.

Within each host, Named Slot content is visited before ordinary Panel children,
and Panel children preserve `GetChildAt` order. The top-level root traversal is
emitted before WidgetTree-level inherited Named Slot content. Where UE exposes
no authored order, the adapter must still return deterministic text rather than
claim Panel sibling order.

### Widget Collection

`widgets` discovers source objects without expanding their structural context:

```lgl
query menu
widgets "Start"
where type = "/Script/UMG.Button" and reachable
order by name asc
page limit 50
```

The adapter enumerates every source Widget through
`UBaseWidgetBlueprint::ForEachSourceWidget`, including detached Widgets that a
root traversal would miss. Search text matches only stable editor discovery
identity:

- UObject name;
- authored `DisplayLabel` and `GetLabelTextWithMetadata()` text;
- native Class name and Class display name;
- exact native Class path.

It does not search arbitrary values such as `Text`, `Brush`, or nested native
properties. Exact value filtering belongs in `where` or a later exact query.

Each result is a compact `widget(id, type)` binding. `DisplayLabel` appears only
when it is authored and differs meaningfully from the object name. Collection
bindings are result aliases, not claims that each item is a root. No ancestor,
descendant, `Slot`, `NamedSlots`, or schema expansion is returned. A detached
item receives an adjacent ordinary comment.

The first supported `where` fields are `name`, `id`, `type`, `DisplayLabel`,
`bIsVariable`, and `reachable`. Explicit ordering supports `name`, `type`, and
`id`, each ascending or descending. Without search text, reachable Widgets use
tree reading order and detached Widgets follow by name. With search text,
results order by match score, then reachable tree order, then detached name.
Pagination uses the shared cursor model and defaults to 50 items.

`widgets` does not accept `with schema`. The agent first discovers the exact
name or id, then uses an exact Widget query.

### Exact Widget

The name and stable-id forms return the same object state; only resolution
differs:

```lgl
query menu
widget StartButton

query menu
widget@start-guid
```

`widget <name>` matches the exact UObject name, not `DisplayLabel` or localized
Class display text. `widget@id` resolves the corresponding entry in
`WidgetVariableNameToGuidMap`.

The default result contains:

1. Asset and Blueprint context.
2. The shortest compact ancestor chain needed to show the target relationship.
3. The target Widget's `id`, exact native `type`, all readable non-default
   native fields, Panel `Slot`, and `NamedSlots` relationships.

Ancestors carry only navigation identity and relationship state. Descendants
are not expanded. A detached Widget has no invented ancestor chain and receives
an explicit adjacent comment.

Exact reads accept `with schema`:

```lgl
query menu
widget@title-guid
with schema
```

The schema applies to the target and its nested `Slot` and `NamedSlots`
surfaces, not to compact ancestors. It reports exact readable, writable,
resettable, and default behavior plus currently available adapter Operations.

Missing GUID entries are not synthesized during a read. Zero matches return an
unknown-object diagnostic; duplicate name or GUID state returns an ambiguity or
invalid-state diagnostic rather than guessing.

The normalized JSON operation kinds for these Widget queries still require a
separate schema review. This text contract does not silently reuse the
experiment's earlier Widget query payloads.

## Palette

Every Widget created directly by `add` starts from the Palette of the bound
WidgetBlueprint:

```lgl
query menu
palette entries "Button"

Button = widget(palette: "P_Button")
```

An exact read returns the same copyable creation binding and may add its schema
as structured comments:

```lgl
query menu
palette @P_Button
with schema

Button = widget(palette: "P_Button")
```

All Widget Palette entries use `widget(...)`. Native Widget Classes, loaded or
unloaded WidgetBlueprint assets, image-asset templates, and plugin templates do
not produce separate Class constructors or a `widget(class: ...)` branch. The
opaque Palette id selects the exact `FWidgetTemplate` creation capability.

The Palette call does not repeat `type` when the selected capability already
fixes the created Class. `with schema` reports the resulting native `type`,
current context constraints, native defaults, editable fields, and initially
available Operations. Properties are edited after creation through ordinary
ordered `set` statements rather than friendly constructor arguments.

`FWidgetTemplate::Create` may create one Widget or a native subtree. The Palette
binding names its primary returned Widget. Any descendants produced by that
same template are native effects, not additional direct `add` inputs. The
mutation result returns the complete materialized subtree in ordinary ordered
Widget Object Text with real Widget ids.

At `add`, the adapter resolves the Palette id again and validates at least:

1. The exact template is still available in the target WidgetBlueprint context.
2. Its Widget Class is usable and not abstract, deprecated, hidden, or excluded
   by project Palette settings.
3. A User Widget does not introduce a circular WidgetBlueprint reference.
4. The requested Widget name is valid and unused.
5. The selected parent can accept the created primary Widget.

A Palette capability id is not a future Widget id and is not persisted in
materialized Widget Object Text. Display labels, Class display names, and menu
positions are never used as execution identity.

Palette results are ordinary ordered bindings with ordinary `Call` values.
Widget defines no `PaletteResult`, `ShortcutEntry`, `ClassEntry`, or
Widget-specific creation expression.

## Patch

Widget Patch uses the shared lifecycle operations. Bindings and operations are
separate ordered statements.

Creating the root Widget uses a local binding:

```lgl
patch menu

mainCanvas = widget(palette: "P_CanvasPanel")
add mainCanvas
```

For Widget Patch, direct `add` of a local Widget binding means set it as the
WidgetTree root. It is valid only when no root currently exists.

Creating a child uses a member binding whose owner is the parent Widget:

```lgl
patch menu

stack.start = widget(palette: "P_Button")
add stack.start
set stack.start.Text = "<FText native text>"
```

The adapter materializes the Palette template in the target `UWidgetTree`, uses
the binding member as the exact requested Widget name, attaches the Widget
through the resolved parent, creates the native Panel Slot when applicable, and
registers every created source Widget through the WidgetBlueprint GUID path.

Existing objects use typed stable references across requests:

```lgl
set widget@title-guid.Text = "<FText native text>"
reset widget@title-guid.Text
move widget@help-guid after widget@start-guid
remove widget@quit-guid
```

The confirmed Widget lifecycle surface is:

| Operation | Meaning |
| --- | --- |
| `add binding` | create one Palette-backed root or child Widget |
| `set widget.field = value` | write one schema-approved native Widget field |
| `reset widget.field` | restore one field through its schema-approved native reset path |
| `move widget before|after widget` | reorder siblings under the same parent |
| `remove widget` | remove one authored Widget subtree through native editor behavior |
| `invoke widget Operation(...)` | execute a specialized operation returned by that Widget's schema |

There is no inline `add parent.child = widget(...)` form. A failed validation
applies nothing, and all mutation results use the same ordered Widget Object
Text as queries plus the shared mutation diagnostics and effects.

## Relationship Mutation Boundary

This revision closes the read model for Panel Slots and Named Slots without
pretending they share one UE object:

- Panel placement creates the exact native `UPanelSlot`; its editable fields
  use `set widget@id.Slot.Field` and `reset widget@id.Slot.Field`.
- Named Slot content is returned through the host's `NamedSlots` relationship
  map and never receives a fake Panel Slot.

Root creation and ordinary Panel child creation continue to use the confirmed
Palette-backed `add` flow. Attaching, replacing, or moving content through a
Named Slot is not assigned new Patch syntax by this query revision. Until that
mutation path is reviewed separately, the adapter returns an
unsupported-capability diagnostic instead of interpreting an ambiguous member
binding as a Named Slot edit.

## UE Mapping

The adapter follows UE's native ownership and editor paths:

- `UBaseWidgetBlueprint::WidgetTree` owns the authored tree.
- `UWidgetTree::RootWidget` is the unique root.
- `UBaseWidgetBlueprint::ForEachSourceWidget` enumerates every source Widget,
  including detached objects outside a reachable root traversal.
- `UWidgetTree::ForWidgetAndChildren` visits `INamedSlotInterface` content
  before ordinary `UPanelWidget` children.
- `UPanelSlot` stores the concrete Panel relationship and layout state, while
  `INamedSlotInterface::GetSlotNames` and `GetContentForSlot` expose Named Slot
  relationships without requiring a Slot UObject.
- `FWidgetTemplate::Create(UWidgetTree*)` is the Palette creation path and may
  preserve template-specific initialization.
- `UWidgetBlueprint::WidgetVariableNameToGuidMap` supplies Widget `id` values;
  `OnVariableAdded`, `OnVariableRenamed`, and `OnVariableRemoved` maintain them.
- `UWidgetTree::RemoveWidget`, parent `UPanelWidget` operations, and the Widget
  editor utilities own structural mutation behavior.

The adapter must not replace an exact `FWidgetTemplate` with a guessed Class and
raw `ConstructWidget`, silently suffix requested names, synthesize missing ids
during a read, shorten native Class paths, or collapse Panel Slot and Named Slot
relationships into one field.

## Adapter Boundary

Pure LGL normalization may:

- parse every `widget(...)` value as an ordinary `Call`;
- preserve local and member binding targets and exact statement order;
- normalize typed `widget@id` references and shared query or Patch clauses.

Pure LGL normalization must not:

- resolve a WidgetBlueprint, WidgetTree, Widget, Palette capability, or Class;
- choose a constructor name from a native Class;
- validate Widget property names, values, parent compatibility, or capacity;
- infer a root, Panel Slot, or Named Slot relationship not present in the
  ordered document;
- compile the WidgetBlueprint or bind Widget events to Graph Nodes.

Those UE-dependent responsibilities belong to the Widget adapter and Bridge.
