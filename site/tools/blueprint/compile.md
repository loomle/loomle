---
layout: default
title: Compile and Save
parent: Blueprint
grand_parent: Interfaces
nav_order: 6
---

# Compile and Save

Compilation targets one complete Blueprint, never one Graph. Finalization is a
separate terminal Patch so authored edits and their validation remain distinct:

```text
door = blueprint(
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "blueprint-guid"
)

patch door
compile
save
```

Valid terminal forms are:

```text
patch door
compile

patch door
save

patch door
compile
save
```

They cannot be mixed with bindings or source mutations in the same Patch.

Compilation returns native Blueprint Status and ordered compiler diagnostics.
Save persists only the exact owning Package. Graph and Widget edits therefore
finish through their owning Blueprint after the authored Patch succeeds.
