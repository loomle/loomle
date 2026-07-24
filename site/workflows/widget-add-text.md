---
layout: default
title: Add Widget Text
parent: Workflows
nav_order: 3
---

# Add Widget Text

Read the current authored tree and choose the exact parent:

```text
menu = blueprint(
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "blueprint-guid"
)

query menu
tree depth 20
```

Find the UE Widget creation capability:

```text
menu = blueprint(
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "blueprint-guid"
)

query menu
palette entries "TextBlock"
```

Inspect the chosen Palette Entry with exact schema:

```text
menu = blueprint(
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "blueprint-guid"
)

query menu
palette @palette-entry-id
with schema
```

Then copy the returned Widget constructor into a dry run:

```text
menu = blueprint(
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "blueprint-guid"
)

patch menu dry run
label = widget(palette: "palette-entry-id")
add label to widget@panel-guid
set label.Text = "Ready"
```

Exact schema determines whether `Text`, Slot state, and the chosen placement
are writable for the current object. Apply the authored Widget Patch, then
compile and save the same Widget Blueprint through a separate Blueprint Patch.
