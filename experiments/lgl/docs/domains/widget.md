# Widget Domain

## Scope

The Widget domain describes the authored `UWidget` objects inside a
`UWidgetBlueprint::WidgetTree`. It does not introduce a parallel Widget
document object or translate Widget Classes and properties into LGL-specific
types.

This document is the normative LGL design. The current TypeScript experiment
and UE-backed Widget adapter predate it and remain implementation gaps. Bridge
work is intentionally deferred until the LGL documents are complete.

The current Widget contract covers the authored Widget tree, exact native
Widget and Slot state, Palette-backed creation, structural editing, and Widget
Graph events backed by multicast delegates.

The following UE systems are intentionally outside the current contract:

- Widget Animations and their MovieScene timelines;
- Widget Navigation;
- legacy property and function bindings stored in
  `UWidgetBlueprint::Bindings`;
- the separate Model-View-ViewModel plugin binding system.

LGL does not currently define discovery, query, or mutation behavior for these
systems. Widget lifecycle operations must still preserve or report their native
UE reference and cascade effects; that requirement does not make the deferred
systems independently readable or editable.

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

The target remains one ordinary `blueprint(...)` binding. After UE loads the
object, its actual `UWidgetBlueprint` Class composes the shared Blueprint
capabilities with the Widget capabilities in this document. The public request
does not select a second Widget adapter or carry a `domain` field. Consequently
the target has one combined `summary` and one target-specific `palette`, not
competing Blueprint and Widget variants.

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
  Status: BS_Dirty,
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
  Status: BS_Dirty,
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

Widget queries use the same Blueprint locator chain:

```lgl
menu = blueprint(
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "blueprint-guid"
)
```

The first discovery Query may omit `id` and resolve by exact Asset Path so the
adapter can return `BlueprintGuid`. Later exact Queries use both `asset` and
`id`; Patch requires both. `widget@id` is scoped inside that resolved
WidgetBlueprint and cannot replace the top-level `menu` target.

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

The resolved `UWidgetBlueprint` has one target-specific `summary`. It combines
the useful Blueprint directory with the Widget root identity when one exists
and ordered comments for counts such as total source Widgets, reachable
Widgets, and detached Widgets. It reuses the request target alias, does not
repeat the complete Asset and Blueprint locator merely for context, does not
expand the tree, and introduces no summary-specific object.

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

The default tree result reuses the request's `menu` binding and returns a
structure-and-layout skeleton:

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
properties. Exact filtering over the supported discovery fields belongs in
`where`; other values require a later exact query.

Each result is a compact `widget(id, type)` binding. `DisplayLabel` appears only
when it is authored and differs meaningfully from the object name. Collection
bindings are result aliases, not claims that each item is a root. No ancestor,
descendant, `Slot`, `NamedSlots`, or schema expansion is returned. A detached
item receives an adjacent ordinary comment.

The collection has one closed structured surface. String fields `name`, `id`,
`type`, and `DisplayLabel` support exact `=` and `!=` only. Boolean fields
`bIsVariable` and `reachable` support `=`, `!=`, and predicate shorthand such
as `reachable` or `not bIsVariable`. Conditions may combine these predicates
through `not`, `and`, `or`, and parentheses. Ordered comparisons and `~=` are
unsupported; fuzzy discovery belongs to the primary search text.

Explicit ordering supports `name`, `type`, and `id`, each ascending or
descending. Without search text, reachable Widgets use tree reading order and
detached Widgets follow by name. With search text, results order by match
score, then reachable tree order, then detached name. Pagination uses the
shared cursor model and defaults to 50 items.

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

1. The shortest compact ancestor chain needed to show the target relationship.
2. The target Widget's `id`, exact native `type`, all readable non-default
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

### Graph Events

An accessible native `FMulticastDelegateProperty` such as `OnClicked` is a
Graph-event capability, not writable Widget state. An exact Widget read with
`with schema` lists it in the existing `fields` section with its native name
and delegate type. It does not introduce an Event object, an `events` query, or
a Widget mutation Operation:

```lgl
query menu
widget@button-guid
with schema

###
schema

fields:
  OnClicked: FOnButtonClickedEvent; graph event
    availability: available
    discover with:
      eventOwner = blueprint(
        asset: "/Game/UI/WBP_Menu.WBP_Menu",
        id: "blueprint-guid"
      )
      eventGraph = graph(asset: eventOwner, id: "event-graph-guid")
      query eventGraph
      palette entries "OnClicked"
      where widget = widget@button-guid

  OnPressed: FOnButtonPressedEvent; graph event
    availability: available
    discover with:
      eventOwner = blueprint(
        asset: "/Game/UI/WBP_Menu.WBP_Menu",
        id: "blueprint-guid"
      )
      eventGraph = graph(asset: eventOwner, id: "event-graph-guid")
      query eventGraph
      palette entries "OnPressed"
      where widget = widget@button-guid
###
```

The indented LGL under `discover with` is one complete copyable query. The
label is schema Comment text, not another LGL keyword. The returned guidance
includes the exact Blueprint owner locator because `graph@id` alone is scoped
and cannot begin a new request. If more than one Ubergraph can own the event,
the schema emits one complete `discover with` block for each compatible Graph
rather than choosing from editor focus or `GetLastEditedUberGraph()`.

If the Widget and delegate already have a bound event Node anywhere in the
Blueprint, schema points directly to it:

```lgl
###
schema

fields:
  OnClicked: FOnButtonClickedEvent; graph event
    availability: existing
    inspect with:
      eventOwner = blueprint(
        asset: "/Game/UI/WBP_Menu.WBP_Menu",
        id: "blueprint-guid"
      )
      eventGraph = graph(asset: eventOwner, id: "event-graph-guid")
      query eventGraph
      node@event-node-guid
###
```

`inspect with` is likewise Comment guidance containing a complete exact Node
query. Bound-event identity is the native component-property plus delegate
pair, while the returned `node@id` remains the Graph Node's stable identity.

A Widget must have a corresponding generated `FObjectProperty` before UE can
create a component-bound event. When it does not, schema explains the concrete
prerequisite:

```lgl
###
schema

fields:
  OnClicked: FOnButtonClickedEvent; graph event
    availability: unavailable
    reason: Widget has no generated FObjectProperty
    requires: bIsVariable = true
###
```

When the resolved Widget schema permits that native field, the agent can make
the Widget a Blueprint-visible variable through the ordinary Widget Patch:

```lgl
patch menu
set widget@button-guid.bIsVariable = true
```

The adapter applies the same transaction and structural-modification behavior
as the Widget Designer variable toggle and refreshes the generated Blueprint
member context before a later Graph Palette query. It must not treat this as an
isolated in-memory Boolean write.

The adapter discovers Graph events through accessible
`FMulticastDelegateProperty` fields and UE's Kismet accessibility rules. It
checks the generated Widget property, delegate owner and visibility,
deprecation state, and any existing `UK2Node_ComponentBoundEvent`. `OnClicked`
and other Graph events never accept `set` or `reset`; creation and deletion
remain Graph operations. Ordinary exact Widget reads do not emit this
capability catalog unless `with schema` is present.

## Palette

`palette` is the one catalog exposed by the resolved target. This section
defines its Widget creation entries; it does not create a parallel
Widget-adapter palette selected by public syntax.

Every new Widget materialized by `add` or by a Palette-backed `wrap` or
`replace` starts from the Palette of the bound WidgetBlueprint:

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

When `add`, `wrap`, or `replace` materializes a binding, the adapter resolves
the Palette id again and validates at least:

1. The exact template is still available in the target WidgetBlueprint context.
2. Its Widget Class is usable and not abstract, deprecated, hidden, or excluded
   by project Palette settings.
3. A User Widget does not introduce a circular WidgetBlueprint reference.
4. Any new Widget name requested by the operation is valid and unused.
5. The selected structural operation can accept the created primary Widget.

A Palette capability id is not a future Widget id and is not persisted in
materialized Widget Object Text. Display labels, Class display names, and menu
positions are never used as execution identity.

Palette results are ordinary ordered bindings with ordinary `Call` values.
Widget defines no `PaletteResult`, `ShortcutEntry`, `ClassEntry`, or
Widget-specific creation expression.

## Patch

The `menu` Patch target must be a `blueprint(...)` locator containing both the
exact Asset Path and verified `BlueprintGuid`. A path-only discovery binding is
not sufficient for mutation.

Widget Patch uses shared lifecycle operations for ordinary object ownership and
two Widget-domain structural operations for native transformations that cannot
be decomposed without losing UE behavior. Bindings and operations remain
separate ordered statements.

Creating the root Widget uses a local binding:

```lgl
patch menu

mainCanvas = widget(palette: "P_CanvasPanel")
add mainCanvas
```

Direct `add` of a local Widget binding means set it as the WidgetTree root. It
is valid only when no root currently exists. The binding is the exact requested
initial Widget name and a document-local alias for later statements.

Creating a child keeps creation identity separate from placement:

```lgl
patch menu

start = widget(palette: "P_Button")
add start to widget@stack-guid
set start.Text = "<FText native text>"
```

The adapter materializes the Palette template in the target `UWidgetTree`, uses
the local binding as the exact requested Widget name, attaches the Widget
through the explicit destination, creates the native Panel Slot when
applicable, and registers every created source Widget through the
WidgetBlueprint GUID path.

`add` supports four Widget placement forms:

```lgl
add root
add child to widget@panel-guid
add child before widget@anchor-guid
add child after widget@anchor-guid
```

The bare form creates the root. `to widget@panel-guid` appends to a compatible
`UPanelWidget`; `before` and `after` infer the Panel from the exact anchor and
insert at that sibling position. Every form materializes exactly one
Palette-backed binding.

A Named Slot is an explicit structural destination rather than a writable
field:

```lgl
header = widget(palette: "P_TextBlock")
add header to widget@area-guid.NamedSlots.Header

body = widget(palette: "P_Overlay")
add body to menu.NamedSlots.Body
```

The Widget destination addresses a local `INamedSlotInterface` host. The
Blueprint destination addresses one inherited Named Slot exposed by the
WidgetTree. The exact slot must exist and be empty. `add` never replaces its
current content implicitly.

Existing objects use typed stable references inside the resolved `menu` target.
Each new request still begins with the complete WidgetBlueprint binding; the
scoped references below then select its Widgets:

```lgl
set widget@title-guid.Text = "<FText native text>"
reset widget@title-guid.Text
move widget@help-guid after widget@start-guid
move widget@help-guid to widget@stack-guid
move widget@title-guid to widget@area-guid.NamedSlots.Header
remove widget@quit-guid
```

`move before|after` both reorders same-Panel siblings and reparents a Widget to
the anchor's Panel. `move ... to widget@panel-guid` appends to a Panel;
`move ... to <host>.NamedSlots.<name>` assigns an empty Named Slot. The source
and destination must be inside the same WidgetBlueprint, the destination must
have capacity, and no operation may make a Widget a child of its own subtree.
The current Root is not a valid `move` source: Root creation uses bare `add`,
while Root-preserving transformation uses `wrap` or `replace` and Root deletion
uses `remove`.

Same-Panel reordering keeps the existing `UPanelSlot`. Reparenting to a Panel
creates the destination's native Slot and imports only compatible old Slot
properties, following Widget Designer drag/drop. Moving to a Named Slot
removes the old Panel Slot; moving from a Named Slot explicitly clears the old
host before attaching the Widget. Every discarded, created, or imported Slot
value is part of preflight and mutation effects.

`set` and `reset` edit only schema-approved Widget fields and nested Panel
`Slot` fields. They never change Root, Panel-child, or Named Slot ownership.
`NamedSlots` is therefore readable relationship state, not a writable field.

`remove` means native deletion, never detach. It removes the target and its
authored descendants, clears their Widget GUID records, delegate bindings,
Desired Focus, and applicable Graph variable nodes, then structurally modifies
the WidgetBlueprint. Preflight reports the complete determinable subtree and
reference effects; failure leaves the entire Patch unchanged.

The complete Widget Patch surface is:

| Operation | Meaning |
| --- | --- |
| `add binding [to destination|before anchor|after anchor]` | create and place one Palette-backed Widget |
| `set widget.field = value` | write one schema-approved native Widget field |
| `reset widget.field` | restore one field through its schema-approved native reset path |
| `move widget to destination|before anchor|after anchor` | relocate or reorder one existing Widget subtree |
| `remove widget` | remove one authored Widget subtree through native editor behavior |
| `wrap widget-or-array with binding` | create one Panel wrapper while preserving the targets' external placement |
| `replace widget with binding-or-widget` | perform one native Widget replacement or content promotion |
| `invoke widget Operation(...)` | execute a specialized operation returned by that Widget's schema |

There is no inline `add parent.child = widget(...)` form. A failed validation
applies nothing, and all mutation results use the same ordered Widget Object
Text as queries plus the shared mutation diagnostics and effects.

### Wrap

`wrap` is a Widget-domain atomic operation, analogous to Graph `insert`. It is
not sugar for `add` followed by `move` because UE preserves the target's
external Panel Slot, Named Slot, or Root position while creating new internal
Slots:

```lgl
patch menu

wrapper = widget(palette: "P_VerticalBox")
wrap widget@title-guid with wrapper
```

One statement may wrap multiple direct Panel siblings with one wrapper:

```lgl
wrapper = widget(palette: "P_VerticalBox")
wrap [widget@title-guid, widget@body-guid] with wrapper
```

The operation materializes the Palette-backed `wrapper` binding itself; a
separate `add wrapper` is invalid. The Palette capability must produce exactly
one primary `UPanelWidget`, and that Panel must have capacity for every target.

One `wrap` statement always creates exactly one wrapper. Multiple targets must
be direct children of the same Panel, must not contain duplicate or
ancestor/descendant selections, and are added to the wrapper in the explicit
array order. The first target supplies the wrapper's external sibling position
and Panel Slot. Other target Slots are removed, and every wrapped target gets a
new native Slot owned by the wrapper. A Root or Named Slot target can only be
wrapped alone; the wrapper takes that exact Root or Named Slot relationship.
Detached Widgets are rejected rather than silently ignored.

UE Widget Designer accepts an unordered selection, silently removes selected
descendants of selected ancestors, and may create multiple wrappers for
different parents or insufficient wrapper capacity. LGL deliberately does not
copy that batch convenience: separate deterministic `wrap` statements express
multiple wrappers.

### Replace

`replace` is the second Widget-domain atomic structural operation. A
Palette-backed replacement follows Widget Designer's native Replace With path:

```lgl
replacement = widget(palette: "P_Border")
replace widget@old-guid with replacement
```

The operation materializes `replacement`, preserves the target's external
Panel Slot, Named Slot, or Root relationship, imports compatible native
properties, and moves existing children when both old and new Widgets are
compatible Panels. It preserves the target's logical Widget `id` through
`WidgetVariableNameToGuidMap` and updates native references through the editor
rename and replacement paths. After success, both the local replacement alias
and the old stable reference resolve to the replacement object. Any required
name change or incompatible property, child, capacity, binding, animation, or
reference effect is reported during preflight.

The same operation promotes existing content through UE's native Replace With
Child and Replace With Named Slot behavior:

```lgl
replace widget@panel-guid with widget@only-child-guid
replace widget@container-guid with widget@named-slot-content-guid
```

The existing replacement must be either the target Panel's only direct child
or direct content of one of the target's Named Slots. It keeps its own `id` and
is promoted into the target's exact external relationship; the old target and
the rest of its subtree are removed. Any other existing-object relationship is
invalid rather than being interpreted as a generic move-and-delete.

### Schema Operations

Widget-wide editor actions that do not define a new ownership grammar remain
target-local Operations discovered through `with schema`.

Rename uses the complete Widget editor path rather than a direct UObject or
`DisplayLabel` field write:

```lgl
invoke widget@start-guid Rename(displayName: "Start Button")
```

It validates and sanitizes the requested display name, renames the template and
preview Widget, preserves the Widget `id`, and updates variable references,
delegate bindings, Desired Focus, animations, navigation bindings, UI
Components, and affected child Blueprints. A collision is a validation error;
the adapter never silently chooses a unique suffix.

Duplicate copies one existing Widget subtree through UE's internal Widget
serialization path and exposes the new root as an output:

```lgl
invoke widget@start-guid Duplicate() as copy
```

The duplicate receives new Widget names and GUIDs, keeps copied native and Slot
state, and is inserted immediately after the source. It is available only when
the source has a Panel parent that permits an unambiguous sibling insertion.
Root, Named Slot, detached, and single-child-parent cases are unavailable
rather than invoking Widget Designer's clipboard fallback or replacing
existing content.

`Cut`, `Copy`, and `Paste` remain editor clipboard commands, not LGL Patch
operations. `Find References` and `Open Widget Blueprint` are discovery or
navigation behavior, not mutations. Widget introduces no `attach`, `detach`,
`reparent`, `promote`, or lifecycle `Delete` Operation aliases.

## Relationship Mutation Boundary

Panel Slots and Named Slots remain different UE relationships even though the
same placement operations can address both:

- Panel placement creates the exact native `UPanelSlot`; its editable fields
  use `set widget@id.Slot.Field` and `reset widget@id.Slot.Field`.
- Named Slot content is returned through the host's `NamedSlots` relationship
  map and never receives a fake Panel Slot.
- `add` and `move` own ordinary placement; `wrap` and `replace` own the two
  native compound transformations that must preserve an external relationship.
- `set` and `reset` never reinterpret either relationship as a property write.

## Blueprint Finalization

`UWidgetBlueprint` uses the existing `blueprint(...)` object shape, so it also
uses the one Blueprint-domain terminal compile path rather than a Widget-specific
operation:

```lgl
patch menu
compile
save
```

`compile` selects UE's registered Widget Blueprint compiler and follows every
target, PIE, Full Compile, diagnostic, dependency, and dry-run rule in the
Blueprint domain. The following Core `save` infers the same WidgetBlueprint's
owning Package. Neither statement may be mixed with Widget tree edits such as
`add`, `move`, `wrap`, `replace`, `set`, or `reset`.

The Widget adapter does not define `CompileWidget`, duplicate compiler flags,
or create another result shape. Exact schema on the `blueprint(...)` target
reports `compile` and `save` under `patch:`; exact schema on an individual
`widget(...)` remains about that Widget's fields, relationships, and interfaces.

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
- Widget Designer hierarchy drop owns Panel and Named Slot placement,
  reparenting, compatible Slot-property import, capacity checks, and circular
  relationship checks.
- `FWidgetBlueprintEditorUtils::WrapWidgets`, replacement utilities,
  `RenameWidget`, `DuplicateWidgets`, and `DeleteWidgets` own their complete
  editor behaviors and cascades.
- `UWidgetTree::RemoveWidget` and parent `UPanelWidget` operations provide the
  lower-level structural behavior used by those editor paths.

The adapter must not replace an exact `FWidgetTemplate` with a guessed Class and
raw `ConstructWidget`, silently suffix requested names, synthesize missing ids
during a read, shorten native Class paths, or collapse Panel Slot and Named Slot
relationships into one field.

## Adapter Boundary

Pure LGL normalization may:

- parse every `widget(...)` value as an ordinary `Call`;
- preserve local and member binding targets and exact statement order;
- normalize typed `widget@id` references, relationship destinations, and the
  Widget-domain `wrap` and `replace` statement shapes;
- preserve shared Blueprint `compile` and Core `save` terminal statement forms
  without selecting a compiler or resolving Package state.

Pure LGL normalization must not:

- resolve a WidgetBlueprint, WidgetTree, Widget, Palette capability, or Class;
- choose a constructor name from a native Class;
- validate Widget property names, values, parent compatibility, or capacity;
- infer a root, Panel Slot, or Named Slot relationship not present in the
  ordered document;
- select or execute the WidgetBlueprint compiler, resolve its owning Package,
  or bind Widget events to Graph Nodes.

Widget-specific responsibilities belong to the Widget adapter and Bridge.
Compilation and saving delegate to the shared Blueprint compile and Core
Package-save services; the Widget adapter must not implement parallel public
semantics.
