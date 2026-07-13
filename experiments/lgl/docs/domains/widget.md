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

A complete tree result begins with its Asset and Blueprint context, then emits
the root and descendants in editor order:

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
  type: "/Script/UMG.VerticalBox"
)

stack.title = widget(
  id: "title-guid",
  type: "/Script/UMG.TextBlock",
  Text: "<FText native text>"
)
```

The unique top-level Widget binding is `UWidgetTree::RootWidget`. It needs no
duplicate `root` argument. An empty WidgetTree has no Widget bindings.

Panel hierarchy uses member binding targets:

```lgl
parent.child = widget(...)
```

The parent alias must already exist. The member name is the child's current UE
object name and introduces its document-local alias for later bindings. Sibling
order is the order of child binding statements under the same parent. A parent
always precedes its descendants. Structural braces, `children` arrays, and a
translated `parent` constructor argument are unnecessary.

Every source Widget's `id` maps to its entry in
`UWidgetBlueprint::WidgetVariableNameToGuidMap`. UE preserves that GUID through
`OnVariableRenamed`, so later requests use `widget@id` rather than a current
name. Query-result aliases remain document-local.

Native Widget properties are flattened onto the `widget(...)` Call only when
they are part of the requested object state. LGL does not rename `Text` to
`text`, split native `Font` data into `fontSize`, or invent friendly Widget
property types. `with schema` discovers the exact readable, writable, and
resettable fields for the resolved Widget Class.

Normalized JSON uses the shared ordered `Binding` and ordinary `Call`. It has
no Widget-specific Document or Node expression:

```json
{
  "target": {"kind": "member", "object": "mainCanvas", "member": "stack"},
  "value": {
    "kind": "call",
    "callee": "widget",
    "args": {
      "id": "stack-guid",
      "type": "/Script/UMG.VerticalBox"
    }
  }
}
```

## Query

Widget reads use the shared summary, collection, exact-name, stable-id, and
Palette model:

```lgl
summary menu

query menu
tree

query menu
widgets ["text"]

query menu
widget <name>

query menu
find widget@id

query menu
palette entries ["text"]

query menu
palette @id
```

`menu` resolves a `UWidgetBlueprint`, not an artificial Widget document. Each
query has exactly one primary operation.

`tree` returns the Asset and Blueprint context followed by the complete authored
Widget hierarchy. `widgets` enumerates or searches compact Widget identities;
its search text covers current name, native `type`, and relevant native
descriptive fields. Structured filtering uses native values:

```lgl
query menu
widgets "Start"
where type = "/Script/UMG.Button"
order by name asc
page limit 50
```

`widget <name>` resolves one current Widget name inside the bound WidgetTree.
`find widget@id` resolves one stable Widget GUID. Exact reads accept
`with schema`; collection search does not recursively expand schemas:

```lgl
query menu
find widget@title-guid
with schema
```

The exact result includes the shortest ancestor chain needed to make its member
binding unambiguous. Zero matches return an unknown-object diagnostic, and
invalid duplicate identity or name state returns an ambiguity diagnostic rather
than guessing.

`summary menu` is owned by the Widget adapter. It returns the compact Blueprint
context and enough root/count comments to orient the agent without expanding
the whole tree or introducing a summary-specific object.

The normalized JSON kinds for these Widget query operations still require a
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

## Slot Boundary

Panel Slots and Named Slots are deliberately not defined in this revision.
They are different UE relationships:

- a `UPanelSlot` is a class-sensitive UObject created by a `UPanelWidget`
  parent-child relationship;
- a Named Slot is owned through `INamedSlotInterface` and its content has no
  `UPanelSlot` parent.

The previous generic `slot:` value lost the Slot Class and could not represent
Named Slot ownership without ambiguity. LGL therefore defines no `slot:` sugar,
`with slots` expansion, or Named Slot binding rule until that relationship is
reviewed separately against UE source.

Ordinary Panel hierarchy remains valid. If a requested read or mutation needs a
Slot relationship that the current contract cannot express losslessly, the
adapter returns an unsupported-capability diagnostic rather than flattening or
guessing it.

## UE Mapping

The adapter follows UE's native ownership and editor paths:

- `UBaseWidgetBlueprint::WidgetTree` owns the authored tree.
- `UWidgetTree::RootWidget` is the unique root.
- `FWidgetTemplate::Create(UWidgetTree*)` is the Palette creation path and may
  preserve template-specific initialization.
- `UWidgetBlueprint::WidgetVariableNameToGuidMap` supplies Widget `id` values;
  `OnVariableAdded`, `OnVariableRenamed`, and `OnVariableRemoved` maintain them.
- `UWidgetTree::RemoveWidget`, parent `UPanelWidget` operations, and the Widget
  editor utilities own structural mutation behavior.

The adapter must not replace an exact `FWidgetTemplate` with a guessed Class and
raw `ConstructWidget`, silently suffix requested names, synthesize missing ids
during a read, or collapse Panel Slot and Named Slot relationships into one
field.

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
