---
layout: default
title: Widget
parent: Interfaces
nav_order: 6
---

# Widget

Widget operates on authored `UWidget` objects inside one
`UWidgetBlueprint::WidgetTree`. Widget and Blueprint are independent Domains
even when they open the same asset.

```text
menu = target {
  domain: widget,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "11111111-1111-1111-1111-111111111111"
}

query menu
tree depth 20
```

The Widget `id` is a canonical lowercase, hyphenated, non-zero Guid.

## Query

Widget adds these queries:

```text
tree
widgets
widget <name>
@widget-guid
```

Summary and Palette belong to the Widget Target. Tree reads return authored
hierarchy and Slot layout; collection search also finds detached Widgets.

Panel Slot state is nested on the child Widget. Named Slot relationships are
also native Widget relationships. Neither is an independent selector, object,
or Palette entry.

## Patch

Create from the combined target Palette, then use structural Widget operations:

```text
menu = target {
  domain: widget,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "11111111-1111-1111-1111-111111111111"
}

patch menu dry run
label = { palette: "palette-entry-id" }
add label to @panel-guid
set label.Text = "Start"
```

Current Patch operations include:

```text
add
move
remove
wrap
replace
set
reset
invoke
```

Exact Widget schema is authoritative for Widget fields, Slot fields, placement
constraints, and available operations.

Widget events such as `OnClicked` are Graph Palette capabilities. Exact Widget
schema may return an independent canonical Graph Target and an explicit
handoff; it never nests that Target in a Widget object. Widget Animation,
Navigation, legacy Binding, and MVVM are outside the current interface.

Finalize through the related Blueprint Target returned by Widget results:

```text
bp = target {
  domain: blueprint,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "11111111-1111-1111-1111-111111111111"
}

patch bp
compile
save
```
