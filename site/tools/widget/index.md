---
layout: default
title: Widget
parent: Interfaces
nav_order: 6
---

# Widget

Widget operates on authored `UWidget` objects inside one
`UWidgetBlueprint::WidgetTree`. It composes the Blueprint interface on the same
target; there is no separate `widget(asset: ...)` document wrapper.

```sal
menu = blueprint(
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "blueprint-guid"
)

query menu
tree depth 20
```

## Query

Widget adds `tree`, `widgets`, `widget <name>`, and `widget@id`. `summary` and
Palette are shared with the Blueprint target. Tree reads return authored
hierarchy and Slot layout; collection search also finds detached Widgets.

Panel Slot state is nested on the child Widget. Named Slot relationships are
also native Widget relationships. Neither is an independent selector, object,
or Palette entry.

## Patch

Create from the combined target Palette, then use structural Widget operations:

```sal
menu = blueprint(
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "blueprint-guid"
)

patch menu dry run
label = widget(palette: "palette-entry-id")
add label to widget@panel-guid
set label.Text = "Start"
```

Current Patch operations include `add`, `move`, `remove`, `wrap`, `replace`,
field `set`/`reset`, and schema-discovered `invoke`. Exact Widget schema is
authoritative for Widget fields, Slot fields, placement constraints, and
available operations.

Widget events such as `OnClicked` are Graph Palette capabilities. Exact Widget
schema returns the locator-complete Graph query to find the event Node. Widget
Animation, Navigation, legacy Binding, and MVVM are outside the current
interface.

Finalize through a separate Blueprint `compile` and `save` Patch.
