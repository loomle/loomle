#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


ENGINE_SOURCE_ROOT = Path("/Users/Shared/Epic Games/UE_5.7/Engine/Source")

UCLASS_RE = re.compile(
    r"UCLASS(?:\s*\((?P<meta>.*?)\))?\s*"
    r"class\s+(?:(?P<api>\w+)\s+)?(?P<name>U\w+)\s*:\s*public\s+(?P<base>U\w+)\s*\{(?P<body>.*?)\n\};",
    re.DOTALL,
)


def clean_ws(value: str) -> str:
    return re.sub(r"\s+", " ", value).strip()


def humanize_identifier(name: str) -> str:
    text = name
    for prefix in ("UMaterialExpression", "UK2Node_", "UK2Node"):
        if text.startswith(prefix):
            text = text[len(prefix) :]
            break
    text = text.replace("_", " ")
    text = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", text)
    text = re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", " ", text)
    text = re.sub(r"(?<=\d)\s+(?=[A-Z])", "", text)
    text = re.sub(r"\bWs\b", "WS", text)
    text = re.sub(r"\bUv\b", "UV", text)
    text = re.sub(r"\bRgb\b", "RGB", text)
    return clean_ws(text)


def parse_property_name(decl: str) -> str | None:
    line = clean_ws(decl.rstrip(";"))
    if "=" in line:
        line = line.split("=", 1)[0].strip()
    match = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*$", line)
    return match.group(1) if match else None


def parse_property_type(decl: str, prop_name: str | None) -> str:
    line = clean_ws(decl.rstrip(";"))
    if "=" in line:
        line = line.split("=", 1)[0].strip()
    line = line.lstrip(") ").strip()
    if prop_name and line.endswith(prop_name):
        return line[: -len(prop_name)].strip()
    return line


def parse_category(meta: str) -> str | None:
    for pattern in (
        r'Category\s*=\s*"([^"]+)"',
        r'Category\s*=\s*([A-Za-z0-9_|]+)',
    ):
        match = re.search(pattern, meta)
        if match:
            return match.group(1)
    return None


def parse_flags(meta: str, extra_flags: tuple[str, ...] = ()) -> list[str]:
    known = [
        "EditAnywhere",
        "VisibleAnywhere",
        "BlueprintReadOnly",
        "BlueprintReadWrite",
        "AdvancedDisplay",
        "Transient",
    ]
    flags = [flag for flag in [*known, *extra_flags] if flag in meta]
    return flags


def extract_uproperty_entries(body: str) -> list[tuple[str, str]]:
    entries: list[tuple[str, str]] = []
    cursor = 0
    while True:
        start = body.find("UPROPERTY(", cursor)
        if start == -1:
            break
        i = start + len("UPROPERTY(")
        depth = 1
        while i < len(body) and depth > 0:
            char = body[i]
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            i += 1
        meta = body[start + len("UPROPERTY(") : i - 1]
        while i < len(body) and body[i].isspace():
            i += 1
        decl_start = i
        while i < len(body) and body[i] != ";":
            i += 1
        decl = body[decl_start : i + 1]
        entries.append((meta, decl))
        cursor = i + 1
    return entries


def get_module_name(path: Path, source_root: Path) -> str:
    parts = path.relative_to(source_root).parts
    return parts[1]


def get_source_group(path: Path, source_root: Path) -> str:
    parts = path.relative_to(source_root).parts
    return f"{parts[0]}/{parts[1]}"


def derive_node_class_path(module_name: str, class_name: str) -> str:
    return f"/Script/{module_name}.{class_name.removeprefix('U')}"


def parse_material_creation_name(body: str, fallback_name: str) -> str:
    patterns = (
        r'GetCreationName\s*\(\)\s*const\s*override\s*\{[^}]*TEXT\("([^"]+)"\)[^}]*\}',
        r'GetDisplayName\s*\(\)\s*const\s*override\s*\{[^}]*TEXT\("([^"]+)"\)[^}]*\}',
    )
    for pattern in patterns:
        match = re.search(pattern, body, re.DOTALL)
        if match:
            return clean_ws(match.group(1))
    return humanize_identifier(fallback_name)


def parse_uclass_meta_flags(raw_meta: str | None) -> list[str]:
    if not raw_meta:
        return []
    flags = []
    for flag in ("Abstract", "MinimalAPI", "BlueprintType", "Blueprintable", "HideDropdown"):
        if flag in raw_meta:
            flags.append(flag)
    return flags


def parse_uclass_headers(pattern: str) -> list[dict]:
    entries: list[dict] = []
    for path in sorted(ENGINE_SOURCE_ROOT.glob(pattern)):
        text = path.read_text(encoding="utf-8", errors="ignore")
        for match in UCLASS_RE.finditer(text):
            name = match.group("name")
            base = match.group("base")
            body = match.group("body")
            raw_meta = match.group("meta")
            module_name = get_module_name(path, ENGINE_SOURCE_ROOT)
            relative_header = path.relative_to(ENGINE_SOURCE_ROOT).as_posix()
            properties = []
            for raw_prop_meta, raw_decl in extract_uproperty_entries(body):
                meta = clean_ws(raw_prop_meta)
                decl = clean_ws(raw_decl)
                prop_name = parse_property_name(decl)
                if not prop_name:
                    continue
                properties.append(
                    {
                        "name": prop_name,
                        "cppType": parse_property_type(decl, prop_name),
                        "declaration": decl,
                        "category": parse_category(meta),
                        "flags": parse_flags(meta),
                        "meta": meta,
                    }
                )
            entries.append(
                {
                    "className": name,
                    "baseClassName": base,
                    "nodeClassPath": derive_node_class_path(module_name, name),
                    "moduleName": module_name,
                    "sourceGroup": get_source_group(path, ENGINE_SOURCE_ROOT),
                    "headerPath": str(path),
                    "relativeHeaderPath": relative_header,
                    "uclassFlags": parse_uclass_meta_flags(raw_meta),
                    "body": body,
                    "properties": properties,
                }
            )
    return entries


def derive_family_material(class_name: str, ancestry: list[str]) -> str:
    if "UMaterialExpressionParameter" in ancestry or "Parameter" in class_name:
        return "parameter"
    if "UMaterialExpressionTextureBase" in ancestry or "Texture" in class_name:
        return "texture"
    if "UMaterialExpressionCustomOutput" in ancestry:
        return "custom_output"
    if class_name.endswith("Comment"):
        return "utility"
    if class_name.startswith("UMaterialExpressionConstant"):
        return "constant"
    return "expression"


def build_material_database() -> dict:
    classes = {entry["className"]: entry for entry in parse_uclass_headers("**/MaterialExpression*.h")}
    derived: dict[str, dict] = {}
    changed = True
    while changed:
        changed = False
        for name, entry in classes.items():
            base = entry["baseClassName"]
            if name in derived:
                continue
            if base == "UMaterialExpression" or base in derived:
                derived[name] = entry
                changed = True

    nodes = []
    parameter_count = 0
    custom_output_count = 0
    for name in sorted(derived):
        entry = derived[name]
        ancestry = [entry["baseClassName"]]
        base = entry["baseClassName"]
        while base in classes:
            next_base = classes[base]["baseClassName"]
            ancestry.append(next_base)
            base = next_base
        family = derive_family_material(name, ancestry)
        parameter_count += family == "parameter"
        custom_output_count += family == "custom_output"
        nodes.append(
            {
                "className": name,
                "baseClassName": entry["baseClassName"],
                "nodeClassPath": entry["nodeClassPath"],
                "displayName": parse_material_creation_name(entry["body"], name),
                "family": family,
                "moduleName": entry["moduleName"],
                "sourceGroup": entry["sourceGroup"],
                "headerPath": entry["headerPath"],
                "relativeHeaderPath": entry["relativeHeaderPath"],
                "uclassFlags": entry["uclassFlags"],
                "isParameterClass": family == "parameter",
                "isTextureClass": family == "texture",
                "isCustomOutputClass": family == "custom_output",
                "properties": entry["properties"],
            }
        )

    source_groups: dict[str, int] = {}
    for node in nodes:
        source_groups[node["sourceGroup"]] = source_groups.get(node["sourceGroup"], 0) + 1

    return {
        "graphType": "material",
        "coverage": "source-derived",
        "source": "UE 5.7 local source scan",
        "jsonPath": "workspace/Loomle/material/catalogs/node-database.json",
        "summary": {
            "purpose": "Full source-derived Material expression database for LOOMLE workspace agents. Use node-index.json for the smaller working set and examples/GUIDE/SEMANTICS for execution guidance.",
            "notes": [
                "This database is generated from local Unreal Engine 5.7 headers.",
                "Display names are exact where a literal creation/display name is available in source, otherwise they are best-effort class-name humanizations.",
                "Use Material GUIDE.md and examples/ for execution patterns, then consult this database for class/property details.",
            ],
            "totalNodeClasses": len(nodes),
            "parameterClasses": parameter_count,
            "customOutputClasses": custom_output_count,
            "sourceGroups": source_groups,
        },
        "specialNodes": [
            {
                "nodeId": "__material_root__",
                "displayName": "Material Root",
                "kind": "root_sink",
                "pins": ["Base Color", "Roughness"],
                "usage": "Special graph sink for material properties. Connect expression outputs here rather than treating it like a normal expression node.",
            }
        ],
        "nodes": nodes,
    }


BLUEPRINT_NOT_DIRECT = {
    "UK2Node",
    "UK2Node_EditablePinBase",
    "UK2Node_CallFunction",
    "UK2Node_Event",
    "UK2Node_Variable",
    "UK2Node_StructMemberGet",
    "UK2Node_StructMemberSet",
    "UK2Node_Composite",
    "UK2Node_Tunnel",
}


BLUEPRINT_WITH_ARGS: dict[str, list[str]] = {
    "UK2Node_CallFunction": ["functionClassPath", "functionName"],
    "UK2Node_MacroInstance": ["macroGraphName"],
    "UK2Node_VariableGet": ["variableName"],
    "UK2Node_VariableSet": ["variableName"],
}


def blueprint_lineage(class_name: str, classes: dict[str, dict]) -> list[str]:
    lineage: list[str] = []
    base = classes[class_name]["baseClassName"]
    while base:
        lineage.append(base)
        if base not in classes:
            break
        base = classes[base]["baseClassName"]
    return lineage


def derive_blueprint_kind(class_name: str, lineage: list[str]) -> str:
    lineage_set = set(lineage)
    if "UK2Node_Event" in lineage_set or class_name.endswith("Event"):
        return "event"
    if "UK2Node_Variable" in lineage_set or "Variable" in class_name:
        return "variable"
    if "Delegate" in class_name:
        return "delegate"
    if any(token in class_name for token in ("IfThenElse", "ExecutionSequence", "MultiGate", "DoOnce", "DoN", "FlipFlop")):
        return "control_flow"
    if "CallFunction" in lineage_set or class_name.endswith("CallFunction"):
        return "function"
    if any(token in class_name for token in ("Tunnel", "Composite", "FunctionEntry", "FunctionResult", "MacroInstance")):
        return "graph_structure"
    return "utility"


def derive_blueprint_addability(class_name: str, lineage: list[str]) -> tuple[str, list[str]]:
    lineage_set = set(lineage)
    if class_name in BLUEPRINT_WITH_ARGS:
        return "by_class_with_args", BLUEPRINT_WITH_ARGS[class_name]
    if class_name in BLUEPRINT_NOT_DIRECT:
        return "not_direct", []
    if "UK2Node_Event" in lineage_set or any(token in class_name for token in ("BoundEvent", "InputAction", "InputAxis", "EnhancedInput")):
        return "context_only", []
    if "UK2Node_Composite" in lineage_set or class_name.endswith("Interface"):
        return "action_only", []
    return "by_class", []


def build_blueprint_database() -> dict:
    classes = {entry["className"]: entry for entry in parse_uclass_headers("**/K2Node*.h")}
    derived: dict[str, dict] = {}
    changed = True
    while changed:
        changed = False
        for name, entry in classes.items():
            base = entry["baseClassName"]
            if name in derived:
                continue
            if base == "UK2Node" or base in derived:
                derived[name] = entry
                changed = True

    nodes = []
    addability_counts: dict[str, int] = {}
    node_kind_counts: dict[str, int] = {}
    for name in sorted(derived):
        entry = derived[name]
        lineage = blueprint_lineage(name, classes)
        node_kind = derive_blueprint_kind(name, lineage)
        addability, context_requirements = derive_blueprint_addability(name, lineage)
        addability_counts[addability] = addability_counts.get(addability, 0) + 1
        node_kind_counts[node_kind] = node_kind_counts.get(node_kind, 0) + 1
        nodes.append(
            {
                "className": name,
                "baseClassName": entry["baseClassName"],
                "nodeClassPath": entry["nodeClassPath"],
                "displayName": humanize_identifier(name),
                "nodeKind": node_kind,
                "addability": addability,
                "contextRequirements": context_requirements,
                "moduleName": entry["moduleName"],
                "sourceGroup": entry["sourceGroup"],
                "headerPath": entry["headerPath"],
                "relativeHeaderPath": entry["relativeHeaderPath"],
                "uclassFlags": entry["uclassFlags"],
                "properties": entry["properties"],
            }
        )

    source_groups: dict[str, int] = {}
    for node in nodes:
        source_groups[node["sourceGroup"]] = source_groups.get(node["sourceGroup"], 0) + 1

    return {
        "graphType": "blueprint",
        "coverage": "source-derived",
        "source": "UE 5.7 local source scan",
        "jsonPath": "workspace/Loomle/blueprint/catalogs/node-database.json",
        "summary": {
            "purpose": "Full source-derived Blueprint K2 node database for LOOMLE workspace agents. Use node-index.json for the smaller working set and examples/GUIDE/SEMANTICS for execution guidance.",
            "notes": [
                "This database is generated from local Unreal Engine 5.7 headers.",
                "Function-library calls such as Delay or Print String are usually realized as K2Node_CallFunction instances and therefore belong in the curated node index rather than as standalone source classes here.",
                "Addability is a best-effort classification that distinguishes direct by-class creation from context-only or helper-style nodes.",
            ],
            "totalNodeClasses": len(nodes),
            "addabilityCounts": addability_counts,
            "nodeKindCounts": node_kind_counts,
            "sourceGroups": source_groups,
        },
        "nodes": nodes,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--graph-type",
        choices=("material", "blueprint", "all"),
        default="all",
        help="Which workspace node database(s) to generate.",
    )
    parser.add_argument(
        "--workspace-root",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "workspace" / "Loomle",
        help="Workspace/Loomle root directory.",
    )
    args = parser.parse_args()

    outputs: list[tuple[Path, dict]] = []
    if args.graph_type in ("material", "all"):
        outputs.append((args.workspace_root / "material" / "catalogs" / "node-database.json", build_material_database()))
    if args.graph_type in ("blueprint", "all"):
        outputs.append((args.workspace_root / "blueprint" / "catalogs" / "node-database.json", build_blueprint_database()))

    for path, payload in outputs:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"[wrote] {path}")


if __name__ == "__main__":
    main()
