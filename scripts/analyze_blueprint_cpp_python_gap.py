#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare LoomleBlueprintAdapter C++ dependencies against Python-visible Unreal APIs."
    )
    parser.add_argument(
        "--adapter-cpp",
        default="Loomle/Plugins/LoomleBridge/Source/LoomleBridge/Private/LoomleBlueprintAdapter.cpp",
        help="Path to LoomleBlueprintAdapter.cpp",
    )
    parser.add_argument(
        "--python-inventory",
        default="Loomle/runtime/unreal_native_api_inventory_full.json",
        help="Path to full Python API inventory JSON.",
    )
    parser.add_argument(
        "--output",
        default="Loomle/runtime/blueprint_cpp_python_gap_report.json",
        help="Output JSON path.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    adapter_cpp = Path(args.adapter_cpp)
    inv_path = Path(args.python_inventory)
    out_path = Path(args.output)

    text = adapter_cpp.read_text(encoding="utf-8")
    inventory = json.loads(inv_path.read_text(encoding="utf-8"))
    py_classes = set(inventory.get("classes", {}).keys())

    includes = re.findall(r'^#include\s+"([^"]+)"', text, flags=re.M)
    key_includes = [
        inc
        for inc in includes
        if any(
            marker in inc
            for marker in (
                "K2Node_",
                "EdGraph",
                "Kismet2/",
                "Blueprint.h",
                "SimpleConstructionScript",
                "SCS_Node",
                "AssetRegistry",
                "Components/",
            )
        )
    ]

    static_calls = re.findall(r"\b([A-Za-z_][A-Za-z0-9_:]*)::([A-Za-z_][A-Za-z0-9_]*)\s*\(", text)
    external_static = []
    for namespace, method in static_calls:
        if namespace in ("ULoomleBlueprintAdapter", "LoomleBlueprintAdapterInternal"):
            continue
        external_static.append(f"{namespace}::{method}")
    external_static = sorted(set(external_static))

    include_visibility = []
    for inc in key_includes:
        base = Path(inc).name.replace(".h", "")
        candidates = [base]
        if base.startswith(("U", "F", "A")) and len(base) > 1 and base[1].isupper():
            candidates.append(base[1:])
        found = [candidate for candidate in candidates if candidate in py_classes]
        include_visibility.append(
            {
                "include": inc,
                "classCandidates": candidates,
                "pythonVisible": bool(found),
                "pythonClassesFound": found,
            }
        )

    critical = [
        {"cpp": "FKismetEditorUtilities::CreateBlueprint", "py_class": "BlueprintEditorLibrary", "py_method": "create_blueprint_asset_with_parent"},
        {"cpp": "FKismetEditorUtilities::AddDefaultEventNode", "py_class": "K2Node_Event", "py_method": None},
        {"cpp": "FBlueprintEditorUtils::FindEventGraph", "py_class": "BlueprintEditorLibrary", "py_method": "find_event_graph"},
        {"cpp": "FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified", "py_class": "BlueprintEditorLibrary", "py_method": None},
        {"cpp": "UEdGraphSchema_K2::TryCreateConnection", "py_class": "EdGraphSchema_K2", "py_method": "try_create_connection"},
        {"cpp": "UK2Node_CallFunction::SetFromFunction", "py_class": "K2Node_CallFunction", "py_method": "set_from_function"},
        {"cpp": "UK2Node_VariableGet", "py_class": "K2Node_VariableGet", "py_method": None},
        {"cpp": "UK2Node_VariableSet", "py_class": "K2Node_VariableSet", "py_method": None},
        {"cpp": "UK2Node_DynamicCast", "py_class": "K2Node_DynamicCast", "py_method": None},
        {"cpp": "UK2Node_Knot", "py_class": "K2Node_Knot", "py_method": None},
    ]

    critical_report = []
    all_classes = inventory.get("classes", {})
    for item in critical:
        class_info = all_classes.get(item["py_class"])
        class_visible = class_info is not None
        method_available = None
        if item["py_method"] is not None:
            methods = set(class_info.get("methods", [])) if class_info else set()
            method_available = item["py_method"] in methods
        critical_report.append(
            {
                **item,
                "pythonClassVisible": class_visible,
                "pythonMethodAvailable": method_available,
            }
        )

    result = {
        "source": str(adapter_cpp),
        "pythonInventory": str(inv_path),
        "summary": {
            "adapterExternalStaticSymbols": len(external_static),
            "keyIncludesChecked": len(include_visibility),
            "pythonVisibleIncludeClasses": sum(1 for entry in include_visibility if entry["pythonVisible"]),
            "pythonMissingIncludeClasses": sum(1 for entry in include_visibility if not entry["pythonVisible"]),
            "criticalItems": len(critical_report),
            "criticalMissing": sum(
                1
                for entry in critical_report
                if (not entry["pythonClassVisible"]) or (entry["pythonMethodAvailable"] is False)
            ),
        },
        "adapterExternalStaticSymbols": external_static,
        "includeClassVisibility": include_visibility,
        "criticalGapReport": critical_report,
    }

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"ok": True, "output": str(out_path), "summary": result["summary"]}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
