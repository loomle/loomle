#!/usr/bin/env python3
import argparse
import json
from datetime import datetime, timezone
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate Blueprint native-only Python API schema from runtime inventory."
    )
    parser.add_argument(
        "--inventory",
        default="Loomle/runtime/blueprint_python_api_inventory.json",
        help="Path to blueprint Python API inventory JSON.",
    )
    parser.add_argument(
        "--output",
        default="Loomle/runtime/blueprint_native_api_schema.json",
        help="Path to output native-only schema JSON.",
    )
    return parser.parse_args()


def is_native_class(class_name: str) -> bool:
    return not class_name.startswith("Loomle")


def main() -> int:
    args = parse_args()
    inventory_path = Path(args.inventory)
    output_path = Path(args.output)

    data = json.loads(inventory_path.read_text(encoding="utf-8"))
    classes = data.get("classes", {})
    if not isinstance(classes, dict):
        raise RuntimeError("Inventory JSON missing 'classes' object")

    native_classes = {}
    for class_name, class_info in classes.items():
        if not isinstance(class_name, str) or not isinstance(class_info, dict):
            continue
        if not is_native_class(class_name):
            continue
        methods = class_info.get("methods", [])
        attributes = class_info.get("attributes", [])
        native_classes[class_name] = {
            "methods": sorted(m for m in methods if isinstance(m, str)),
            "attributes": sorted(a for a in attributes if isinstance(a, str)),
        }

    schema = {
        "schemaVersion": "1.0.0",
        "source": "blueprint_python_api_inventory.json",
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "engineVersion": data.get("engine_version"),
        "classCount": len(native_classes),
        "classes": native_classes,
    }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(schema, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"Wrote native schema: {output_path} (classes={len(native_classes)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
