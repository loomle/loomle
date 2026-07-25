---
layout: default
title: Add Widget Text
parent: Workflows
nav_order: 3
---

# Add Widget Text

Read the current authored tree and choose the exact parent:

```text
menu = target {
  domain: widget,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "11111111-1111-1111-1111-111111111111"
}

query menu
tree depth 20
```

Find the UE Widget creation capability:

```text
menu = target {
  domain: widget,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "11111111-1111-1111-1111-111111111111"
}

query menu
palette entries "TextBlock"
```

Inspect the chosen Palette Entry with exact schema:

```text
menu = target {
  domain: widget,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "11111111-1111-1111-1111-111111111111"
}

query menu
palette @palette-entry-id
with schema
```

Then copy the returned Widget object fields into a dry run:

```text
menu = target {
  domain: widget,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "11111111-1111-1111-1111-111111111111"
}

patch menu dry run
label = { palette: "palette-entry-id" }
add label to @panel-guid
set label.Text = "Ready"
```

Exact schema determines whether `Text`, Slot state, and the chosen placement
are writable for the current object. Apply the authored Widget Patch, then use
the returned related Blueprint Target and explicit handoff to compile and save
through a separate Blueprint Patch.
