#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


ENGINE_SOURCE_ROOT = Path("/Users/Shared/Epic Games/UE_5.7/Engine/Source")
PCG_PUBLIC_ROOT = Path("/Users/Shared/Epic Games/UE_5.7/Engine/Plugins/PCG/Source/PCG/Public")

UCLASS_RE = re.compile(
    r"UCLASS(?:\s*\((?P<meta>.*?)\))?\s*"
    r"class\s+(?:(?P<api>\w+)\s+)?(?P<name>U\w+)\s*:\s*public\s+(?P<base>U\w+)\s*\{(?P<body>.*?)\n\};",
    re.DOTALL,
)

PCG_CLASS_RE = re.compile(
    r"class\s+(?P<name>\w+)\s*:\s*public\s+(?P<base>\w+)\s*\{(?P<body>.*?)\n\};",
    re.DOTALL,
)

PCG_SETTINGS_ROOTS = {
    "UPCGSettings",
    "UPCGBaseSubgraphSettings",
    "UPCGSettingsWithDynamicInputs",
}


def clean_ws(value: str) -> str:
    return re.sub(r"\s+", " ", value).strip()


def parse_nsloctext_arg(expr: str) -> str | None:
    match = re.search(r'NSLOCTEXT\s*\(\s*"[^"]*"\s*,\s*"[^"]*"\s*,\s*"([^"]*)"\s*\)', expr)
    if match:
        return match.group(1)
    match = re.search(r'FText::FromString\s*\(\s*TEXT\("([^"]*)"\)\s*\)', expr)
    if match:
        return match.group(1)
    return None


def parse_fname_arg(expr: str) -> str | None:
    match = re.search(r'FName\s*\(\s*TEXT\("([^"]*)"\)\s*\)', expr)
    if match:
        return match.group(1)
    match = re.search(r'FName\s*\(\s*"([^"]*)"\s*\)', expr)
    if match:
        return match.group(1)
    return None


def humanize_identifier(name: str) -> str:
    text = name
    for prefix in ("UMaterialExpression", "UK2Node_", "UK2Node"):
        if text.startswith(prefix):
            text = text[len(prefix) :]
            break
    if text.startswith("UPCG") and text.endswith("Settings"):
        text = text[len("UPCG") : -len("Settings")]
    elif text.startswith("UPCG"):
        text = text[len("UPCG") :]
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


def parse_pcg_flags(meta: str) -> list[str]:
    return parse_flags(meta, extra_flags=("PCG_Overridable",))


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


def parse_pcg_headers(public_root: Path) -> list[dict]:
    entries: list[dict] = []
    for path in sorted(public_root.rglob("*.h")):
        text = path.read_text(encoding="utf-8", errors="ignore")
        for match in PCG_CLASS_RE.finditer(text):
            name = match.group("name")
            base = match.group("base")
            body = match.group("body")
            rel = path.relative_to(public_root).as_posix()
            source_group = rel.split("/", 1)[0] if "/" in rel else rel
            title_match = re.search(r"GetDefaultNodeTitle\s*\(\)\s*const\s*override\s*\{([^}]*)\}", body, re.DOTALL)
            tooltip_match = re.search(r"GetNodeTooltipText\s*\(\)\s*const\s*override\s*\{([^}]*)\}", body, re.DOTALL)
            name_match = re.search(r"GetDefaultNodeName\s*\(\)\s*const\s*override\s*\{([^}]*)\}", body, re.DOTALL)

            properties = []
            for raw_meta, raw_decl in extract_uproperty_entries(body):
                meta = clean_ws(raw_meta)
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
                        "flags": parse_pcg_flags(meta),
                        "meta": meta,
                    }
                )

            entries.append(
                {
                    "className": name,
                    "baseClassName": base,
                    "nodeClassPath": derive_node_class_path("PCG", name),
                    "moduleName": "PCG",
                    "sourceGroup": source_group,
                    "headerPath": str(path),
                    "relativeHeaderPath": rel,
                    "defaultNodeName": parse_fname_arg(name_match.group(1)) if name_match else None,
                    "defaultNodeTitle": parse_nsloctext_arg(title_match.group(1)) if title_match else None,
                    "nodeTooltip": parse_nsloctext_arg(tooltip_match.group(1)) if tooltip_match else None,
                    "hasDynamicPins": "HasDynamicPins() const override { return true; }" in body,
                    "implementsInputPinProperties": "InputPinProperties() const override" in body,
                    "implementsOutputPinProperties": "OutputPinProperties() const override" in body,
                    "implementsCreateElement": "CreateElement() const override" in body,
                    "implementsSupportsBasePointDataInputs": "SupportsBasePointDataInputs" in body,
                    "properties": properties,
                }
            )
    return entries


def derive_pcg_settings_classes(classes: dict[str, dict]) -> dict[str, dict]:
    derived: dict[str, dict] = {}
    changed = True
    while changed:
        changed = False
        for name, info in classes.items():
            base = info["baseClassName"]
            if name in derived:
                continue
            if base in PCG_SETTINGS_ROOTS or base in derived:
                derived[name] = info
                changed = True
    return derived


def pcg_text_tokens(entry: dict) -> str:
    return " ".join(
        filter(
            None,
            [
                entry["className"],
                entry.get("defaultNodeTitle"),
                entry.get("defaultNodeName"),
            ],
        )
    ).lower()


def derive_pcg_family(entry: dict) -> str:
    class_name = entry["className"]
    title = entry.get("defaultNodeTitle") or ""
    name = entry.get("defaultNodeName") or ""
    text = pcg_text_tokens(entry)

    if "subgraph" in text or class_name in {"UPCGSubgraphSettings", "UPCGLoopSettings"}:
        return "struct"
    if class_name in {"UPCGBranchSettings", "UPCGSwitchSettings", "UPCGQualityBranchSettings"}:
        return "branch"
    if class_name in {"UPCGBooleanSelectSettings", "UPCGMultiSelectSettings", "UPCGQualitySelectSettings"}:
        return "select"
    if "filter attribute elements" in title.lower() or class_name in {
        "UPCGAttributeFilteringSettings",
        "UPCGAttributeFilteringRangeSettings",
        "UPCGDensityFilterSettings",
        "UPCGFilterElementsByIndexSettings",
        "UPCGFilterByIndexSettings",
    }:
        return "filter"
    if class_name in {
        "UPCGFilterByAttributeSettings",
        "UPCGFilterByTagSettings",
        "UPCGFilterByTypeSettings",
    }:
        return "route"
    if "sampler" in text or class_name in {
        "UPCGSelectPointsSettings",
        "UPCGSelectGrammarSettings",
        "UPCGSampleTextureSettings",
    }:
        return "sample"
    if class_name in {
        "UPCGTransformPointsSettings",
        "UPCGProjectionSettings",
        "UPCGApplyScaleToBoundsSettings",
        "UPCGCopyPointsSettings",
        "UPCGAttractSettings",
        "UPCGApplyHierarchySettings",
        "UPCGBoundsModifierSettings",
    }:
        return "transform"
    if "create " in title.lower() or name.startswith("Create") or class_name in {
        "UPCGCreateAttributeSetSettings",
        "UPCGCreateTargetActor",
    }:
        return "create"
    if "spawner" in text or class_name in {
        "UPCGAddComponentSettings",
        "UPCGCreateTargetActor",
    }:
        return "spawn"
    if class_name in {
        "UPCGAddTagSettings",
        "UPCGAttributeNoiseSettings",
        "UPCGPointMatchAndSetSettings",
        "UPCGMatchAndSetAttributesSettings",
        "UPCGSortAttributesSettings",
        "UPCGCreateAttributeSetSettings",
        "UPCGAttributeCastSettings",
        "UPCGCopyAttributesSettings",
        "UPCGMergeAttributesSettings",
        "UPCGDeleteAttributesSettings",
        "UPCGExtractAttributeSettings",
        "UPCGVisualizeAttributeSettings",
        "UPCGDataAttributesToTagsSettings",
        "UPCGTagsToDataAttributesSettings",
        "UPCGTagsToAttributeSetSettings",
        "UPCGSortTagsSettings",
        "UPCGReplaceTagsSettings",
        "UPCGDeleteTagsSettings",
    }:
        return "meta"
    if class_name in {
        "UPCGDataFromActorSettings",
        "UPCGDataFromTool",
        "UPCGGetActorDataLayersSettings",
        "UPCGGetActorPropertySettings",
        "UPCGGetAssetListSettings",
        "UPCGGetAttributesSettings",
        "UPCGGetBoundsSettings",
        "UPCGGetConsoleVariableSettings",
        "UPCGGetExecutionContextSettings",
        "UPCGGetLandscapeSettings",
        "UPCGGetLoopIndexSettings",
        "UPCGGetPCGComponentSettings",
        "UPCGGetPrimitiveSettings",
        "UPCGGetPropertyFromObjectPathSettings",
        "UPCGGetResourcePath",
        "UPCGGetSegmentSettings",
        "UPCGGetSplineControlPointsSettings",
        "UPCGGetSplineSettings",
        "UPCGGetStaticMeshResourceDataSettings",
        "UPCGGetSubgraphDepthSettings",
        "UPCGGetTagsSettings",
        "UPCGGetVirtualTextureSettings",
        "UPCGGetVolumeSettings",
        "UPCGTextureSamplerSettings",
        "UPCGGenericUserParameterGetSettings",
        "UPCGUserParameterGetSettings",
    } or title.lower().startswith("get "):
        return "source"
    if class_name in {
        "UPCGAttributeGetFromIndexSettings",
        "UPCGAttributeGetFromPointIndexSettings",
    }:
        return "predicate"
    if class_name.startswith("UPCGAttribute") or class_name.startswith("UPCGMetadata"):
        return "meta"
    return "source" if title.lower().startswith("get ") else "meta"


def default_pcg_profile_for_family(family: str) -> str:
    return {
        "source": "construct_and_query",
        "create": "read_write_roundtrip",
        "sample": "read_write_roundtrip",
        "transform": "read_write_roundtrip",
        "meta": "read_write_roundtrip",
        "predicate": "construct_and_query",
        "branch": "semantic_family_represented",
        "select": "semantic_family_represented",
        "route": "read_write_roundtrip",
        "filter": "dynamic_pin_probe",
        "spawn": "construct_and_query",
        "struct": "context_recipe_required",
    }[family]


def pick_pcg_representative_fields(entry: dict) -> list[str]:
    properties = entry.get("properties", [])
    by_name = {prop["name"]: prop for prop in properties}

    def is_simple_type(cpp_type: str) -> bool:
        if cpp_type in {"bool", "int32", "int", "float", "double", "FString", "FName", "FVector", "FVector2D", "FRotator"}:
            return True
        if cpp_type.startswith("E"):
            return True
        return False

    preferred_names = (
        "Radius",
        "CellSize",
        "Ratio",
        "RotationMin",
        "bAbsoluteRotation",
        "OffsetMin",
        "FilterMode",
        "TargetAttribute",
        "TagsToAdd",
        "LowerBound",
        "UpperBound",
        "Operator",
        "Attribute",
        "PointsPerSquaredMeter",
    )
    selected: list[str] = [
        name
        for name in preferred_names
        if name in by_name and is_simple_type(by_name[name].get("cppType", ""))
    ][:2]
    if selected:
        return selected

    overridable = [
        prop["name"]
        for prop in properties
        if "PCG_Overridable" in prop.get("flags", []) and is_simple_type(prop.get("cppType", ""))
    ]
    if overridable:
        return overridable[:2]
    editable = [
        prop["name"]
        for prop in properties
        if any(flag in prop.get("flags", []) for flag in ("EditAnywhere", "BlueprintReadWrite", "BlueprintReadOnly"))
        and is_simple_type(prop.get("cppType", ""))
    ]
    return editable[:2]


def pick_pcg_dynamic_triggers(entry: dict) -> list[str]:
    property_names = {prop["name"] for prop in entry.get("properties", [])}
    triggers = [name for name in ("bUseConstantThreshold", "FilterMode", "SelectionMode", "MeshSelectorType") if name in property_names]
    return triggers[:2]


def pick_pcg_workflow_families(entry: dict, family: str) -> list[str]:
    class_name = entry["className"]
    if family == "branch":
        return ["pcg_control_flow"]
    if family == "select":
        return ["pcg_selection_flow"]
    if family == "route":
        return ["pcg_route_filter_chain"]
    if family == "spawn":
        return ["pcg_spawn_chain"]
    if family == "transform":
        return ["pcg_pipeline_insert"]
    if class_name in {"UPCGStaticMeshSpawnerSettings", "UPCGSkinnedMeshSpawnerSettings"}:
        return ["pcg_spawn_chain"]
    return []


def pick_pcg_selector_fields(entry: dict) -> list[str]:
    class_name = entry["className"]
    if class_name == "UPCGFilterByAttributeSettings":
        return ["TargetAttribute"]
    if class_name == "UPCGGetActorPropertySettings":
        return ["ActorSelector", "OutputAttributeName"]
    if class_name == "UPCGStaticMeshSpawnerSettings":
        return ["MeshSelectorParameters"]
    if class_name == "UPCGAttributeRemoveDuplicatesSettings":
        return ["AttributeSelectors"]
    if class_name == "UPCGMetadataPartitionSettings":
        return ["PartitionAttributeSelectors"]
    return []


def derive_pcg_recipe(entry: dict, family: str) -> str | None:
    class_name = entry["className"]
    if class_name in {
        "UPCGDataFromActorSettings",
        "UPCGGetActorPropertySettings",
        "UPCGGetActorDataLayersSettings",
        "UPCGGetPrimitiveSettings",
        "UPCGGetSplineSettings",
        "UPCGGetLandscapeSettings",
        "UPCGGetVolumeSettings",
        "UPCGGetPCGComponentSettings",
        "UPCGGetSplineControlPointsSettings",
    }:
        return "pcg_actor_source_context"
    return None


def derive_pcg_testing(entry: dict) -> dict:
    family = derive_pcg_family(entry)
    profile = default_pcg_profile_for_family(family)
    testing: dict[str, object] = {"profile": profile}
    class_name = entry["className"]

    if class_name == "UPCGDensityFilterSettings":
        testing["profile"] = "read_write_roundtrip"
        testing["focus"] = {"fields": ["LowerBound", "UpperBound"]}
        return testing
    if class_name == "UPCGFilterByTagSettings":
        testing["profile"] = "read_write_roundtrip"
        testing["focus"] = {"fields": ["SelectedTags", "Operation"]}
        return testing
    if class_name == "UPCGAttributeFilteringRangeSettings":
        testing["profile"] = "dynamic_pin_probe"
        testing["focus"] = {
            "dynamicTriggers": [
                "MinThreshold/bUseConstantThreshold",
                "MaxThreshold/bUseConstantThreshold",
            ]
        }
        return testing
    if class_name == "UPCGFilterElementsByIndexSettings":
        testing["profile"] = "read_write_roundtrip"
        testing["focus"] = {"fields": ["SelectedIndices", "bInvertFilter"]}
        return testing
    if class_name == "UPCGFilterByIndexSettings":
        testing["profile"] = "read_write_roundtrip"
        testing["focus"] = {"fields": ["SelectedIndices", "bInvertFilter"]}
        return testing
    if class_name == "UPCGCreateTargetActor":
        testing["profile"] = "read_write_roundtrip"
        testing["focus"] = {"fields": ["bDeleteActorsBeforeGeneration", "CommaSeparatedActorTags"]}
        return testing
    if class_name == "UPCGCreatePointsGridSettings":
        testing["profile"] = "read_write_roundtrip"
        testing["focus"] = {"fields": ["CoordinateSpace", "bCullPointsOutsideVolume"]}
        return testing
    if class_name == "UPCGLoadDataAssetSettings":
        testing["profile"] = "read_write_roundtrip"
        testing["focus"] = {"fields": ["bWarnIfNoAsset", "bSynchronousLoad"]}
        return testing
    if class_name == "UPCGAttributeRemoveDuplicatesSettings":
        testing["profile"] = "construct_and_query"
        testing["reason"] = "Accessible settings truth is selector-backed rather than simple scalar roundtrip."
    if class_name == "UPCGMetadataPartitionSettings":
        testing["profile"] = "construct_and_query"
        testing["reason"] = "Accessible settings truth is selector-backed rather than simple scalar roundtrip."
    if class_name == "UPCGCollapsePointsSettings":
        testing["profile"] = "read_write_roundtrip"
        testing["focus"] = {"fields": ["DistanceThreshold", "bUseMergeWeightAttribute"]}
        return testing
    if class_name == "UPCGPrintElementSettings":
        testing["profile"] = "read_write_roundtrip"
        testing["focus"] = {"fields": ["PrintString", "bDisplayOnNode"]}
        return testing
    if class_name == "UPCGHiGenGridSizeSettings":
        testing["profile"] = "construct_and_query"
        testing["reason"] = "Current graph default-setting path cannot reliably drive this enum-backed setting."
        return testing
    if class_name == "UPCGResetPointCenterSettings":
        testing["profile"] = "construct_and_query"
        testing["reason"] = "Current graph default-setting path cannot reliably drive this vector-backed setting."
        return testing
    if class_name == "UDEPRECATED_PCGGenerateGrassMapsSettings":
        testing["profile"] = "inventory_only"
        testing["reason"] = "Deprecated settings class is not valid for direct node construction."
        return testing

    recipe = derive_pcg_recipe(entry, family)
    if recipe:
        testing["recipe"] = recipe
        if family == "source":
            testing["reason"] = "Depends on world or actor-backed source context."
    elif family == "struct":
        testing["reason"] = "Requires subgraph or loop context rather than isolated node coverage."
    focus: dict[str, object] = {}
    if profile == "read_write_roundtrip":
        fields = pick_pcg_representative_fields(entry)
        if fields:
            focus["fields"] = fields
        else:
            testing["profile"] = "construct_and_query"
            if family in {"meta", "transform", "create"}:
                testing["reason"] = "No stable representative roundtrip fields were identified for first-version coverage."
            if "recipe" in testing:
                testing["reason"] = "Depends on world or actor-backed source context."
            return testing
    elif profile == "dynamic_pin_probe":
        triggers = pick_pcg_dynamic_triggers(entry)
        if triggers:
            focus["dynamicTriggers"] = triggers
    elif profile == "semantic_family_represented":
        workflows = pick_pcg_workflow_families(entry, family)
        if workflows:
            focus["workflowFamilies"] = workflows
    selector_fields = pick_pcg_selector_fields(entry)
    if selector_fields:
        focus["selectorFields"] = selector_fields
        testing.setdefault("reason", "Structured selector truth should be validated separately from scalar roundtrip fields.")
    if focus:
        testing["focus"] = focus
    return testing


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


def default_material_profile_for_family(family: str) -> str:
    return {
        "parameter": "construct_and_query",
        "texture": "construct_and_query",
        "custom_output": "construct_and_query",
        "constant": "construct_only",
        "utility": "construct_only",
        "expression": "construct_and_query",
    }[family]


def pick_material_representative_fields(entry: dict) -> list[str]:
    class_name = entry["className"]
    if class_name in {"UMaterialExpressionScalarParameter", "UMaterialExpressionVectorParameter"}:
        return ["ParameterName", "DefaultValue"]
    if class_name == "UMaterialExpressionCollectionParameter":
        return ["ParameterName", "SortPriority"]
    if class_name == "UMaterialExpressionDynamicParameter":
        return ["ParameterIndex", "DefaultValue"]
    if class_name in {
        "UMaterialExpressionFontSampleParameter",
        "UMaterialExpressionTextureCollectionParameter",
        "UMaterialExpressionTextureSampleParameter",
        "UMaterialExpressionRuntimeVirtualTextureSampleParameter",
        "UMaterialExpressionSparseVolumeTextureSampleParameter",
    }:
        return ["ParameterName"]
    return []


def pick_material_workflow_families(entry: dict) -> list[str]:
    class_name = entry["className"]
    if class_name in {"UMaterialExpressionMultiply", "UMaterialExpressionOneMinus", "UMaterialExpressionLinearInterpolate"}:
        return ["material_root_chain"]
    if class_name in {"UMaterialExpressionTextureSample", "UMaterialExpressionTextureSampleParameter2D"}:
        return ["material_texture_chain"]
    return []


def derive_material_recipe(entry: dict) -> str | None:
    if entry["className"] == "UMaterialExpressionMaterialFunctionCall":
        return "material_function_call"
    return None


def derive_material_testing(entry: dict) -> dict:
    family = entry["family"]
    class_name = entry["className"]
    profile = default_material_profile_for_family(family)
    testing: dict[str, object] = {"profile": profile}

    recipe = derive_material_recipe(entry)
    if recipe:
        testing["profile"] = "context_recipe_required"
        testing["recipe"] = recipe
        testing["reason"] = "Requires a child material function asset and material graph context."
        return testing

    if class_name in {
        "UMaterialExpressionScalarParameter",
        "UMaterialExpressionVectorParameter",
        "UMaterialExpressionCollectionParameter",
        "UMaterialExpressionDynamicParameter",
        "UMaterialExpressionFontSampleParameter",
        "UMaterialExpressionTextureCollectionParameter",
        "UMaterialExpressionTextureSampleParameter",
        "UMaterialExpressionRuntimeVirtualTextureSampleParameter",
        "UMaterialExpressionSparseVolumeTextureSampleParameter",
    }:
        fields = pick_material_representative_fields(entry)
        if fields:
            testing["profile"] = "read_write_roundtrip"
            testing["focus"] = {"fields": fields}
            return testing

    workflows = pick_material_workflow_families(entry)
    if workflows:
        testing["focus"] = {"workflowFamilies": workflows}
    return testing


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
    family_counts: dict[str, int] = {}
    profile_counts: dict[str, int] = {}
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
        testing = derive_material_testing(
            {
                "className": name,
                "family": family,
                "properties": entry["properties"],
            }
        )
        family_counts[family] = family_counts.get(family, 0) + 1
        profile = str(testing["profile"])
        profile_counts[profile] = profile_counts.get(profile, 0) + 1
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
                "testing": testing,
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
                "Testing metadata in nodes[].testing is LOOMLE test strategy data, not engine truth.",
            ],
            "totalNodeClasses": len(nodes),
            "parameterClasses": parameter_count,
            "customOutputClasses": custom_output_count,
            "familyCounts": family_counts,
            "testingProfileCounts": profile_counts,
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


def build_pcg_database() -> dict:
    classes = {entry["className"]: entry for entry in parse_pcg_headers(PCG_PUBLIC_ROOT)}
    settings_classes = derive_pcg_settings_classes(classes)
    nodes = []
    family_counts: dict[str, int] = {}
    profile_counts: dict[str, int] = {}
    for name in sorted(
        settings_classes,
        key=lambda class_name: (
            settings_classes[class_name]["sourceGroup"],
            settings_classes[class_name].get("defaultNodeTitle") or settings_classes[class_name]["className"],
            class_name,
        ),
    ):
        entry = settings_classes[name]
        family = derive_pcg_family(entry)
        testing = derive_pcg_testing(entry)
        family_counts[family] = family_counts.get(family, 0) + 1
        profile = str(testing["profile"])
        profile_counts[profile] = profile_counts.get(profile, 0) + 1
        display_name = entry.get("defaultNodeTitle") or humanize_identifier(entry.get("defaultNodeName") or name)
        nodes.append(
            {
                "className": name,
                "baseClassName": entry["baseClassName"],
                "nodeClassPath": entry["nodeClassPath"],
                "displayName": display_name,
                "family": family,
                "moduleName": entry["moduleName"],
                "sourceGroup": entry["sourceGroup"],
                "headerPath": entry["headerPath"],
                "relativeHeaderPath": entry["relativeHeaderPath"],
                "defaultNodeName": entry.get("defaultNodeName"),
                "defaultNodeTitle": entry.get("defaultNodeTitle"),
                "nodeTooltip": entry.get("nodeTooltip"),
                "hasDynamicPins": entry["hasDynamicPins"],
                "implementsInputPinProperties": entry["implementsInputPinProperties"],
                "implementsOutputPinProperties": entry["implementsOutputPinProperties"],
                "implementsCreateElement": entry["implementsCreateElement"],
                "implementsSupportsBasePointDataInputs": entry["implementsSupportsBasePointDataInputs"],
                "properties": entry["properties"],
                "testing": testing,
            }
        )

    source_groups: dict[str, int] = {}
    for node in nodes:
        source_groups[node["sourceGroup"]] = source_groups.get(node["sourceGroup"], 0) + 1

    return {
        "graphType": "pcg",
        "coverage": "source-derived",
        "source": "UE 5.7 local source scan",
        "jsonPath": "workspace/Loomle/pcg/catalogs/node-database.json",
        "summary": {
            "purpose": "Full source-derived PCG settings database for LOOMLE workspace agents. Use node-index.json for the smaller curated working set and GUIDE/SEMANTICS/examples for execution guidance.",
            "notes": [
                "This database is generated from local Unreal Engine 5.7 PCG public headers.",
                "Some node display names come from exact default node titles; others are best-effort humanizations of default node names or class names.",
                "Testing metadata in nodes[].testing is LOOMLE test strategy data, not engine truth.",
            ],
            "totalNodeClasses": len(nodes),
            "dynamicPinClasses": sum(1 for node in nodes if node["hasDynamicPins"]),
            "familyCounts": family_counts,
            "testingProfileCounts": profile_counts,
            "sourceGroups": source_groups,
        },
        "nodes": nodes,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--graph-type",
        choices=("material", "blueprint", "pcg", "all"),
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
    if args.graph_type in ("pcg", "all"):
        outputs.append((args.workspace_root / "pcg" / "catalogs" / "node-database.json", build_pcg_database()))

    for path, payload in outputs:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"[wrote] {path}")


if __name__ == "__main__":
    main()
