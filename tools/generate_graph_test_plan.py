#!/usr/bin/env python3
import argparse
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[1]
WORKSPACE_ROOT = REPO_ROOT / "workspace" / "Loomle"
PLAN_VERSION = "1"

SUPPORTED_GRAPH_TYPES = {"blueprint", "material", "pcg"}
AUTO_CASE_PROFILES = {"construct_only", "construct_and_query", "read_write_roundtrip", "dynamic_pin_probe"}

FIXTURE_REGISTRY: dict[str, dict[str, dict[str, Any]]] = {
    "blueprint": {
        "blueprint_function_graph": {
            "graphType": "blueprint",
            "summary": "Minimal editable Blueprint function graph.",
        },
        "blueprint_event_graph": {
            "graphType": "blueprint",
            "summary": "Minimal editable Blueprint event graph.",
        },
        "blueprint_local_chain": {
            "graphType": "blueprint",
            "summary": "Blueprint exec chain with stable upstream and downstream interfaces.",
        },
    },
    "material": {
        "material_graph": {
            "graphType": "material",
            "summary": "Minimal editable Material graph with a valid root.",
        },
        "material_root_chain": {
            "graphType": "material",
            "summary": "Material graph with a minimal valid expression chain already connected to root.",
        },
        "material_function_graph": {
            "graphType": "material",
            "summary": "Minimal editable Material Function graph with valid function context.",
        },
    },
    "pcg": {
        "pcg_graph": {
            "graphType": "pcg",
            "summary": "Minimal editable PCG graph with clean baseline structure.",
        },
        "pcg_pipeline": {
            "graphType": "pcg",
            "summary": "Minimal legal linear PCG dataflow pipeline.",
        },
        "pcg_pipeline_with_branch": {
            "graphType": "pcg",
            "summary": "Minimal branched PCG pipeline with observable dual-route interfaces.",
        },
        "pcg_graph_with_world_actor": {
            "graphType": "pcg",
            "summary": "PCG graph with stable world actor context available for source nodes.",
        },
    }
}

RECIPE_REGISTRY: dict[str, dict[str, dict[str, Any]]] = {
    "blueprint": {
        "blueprint_variable_access": {
            "graphType": "blueprint",
            "fixture": "blueprint_function_graph",
        },
        "blueprint_event_entry": {
            "graphType": "blueprint",
            "fixture": "blueprint_event_graph",
        },
        "blueprint_function_call": {
            "graphType": "blueprint",
            "fixture": "blueprint_function_graph",
        },
    },
    "material": {
        "material_function_call": {
            "graphType": "material",
            "fixture": "material_graph",
        },
    },
    "pcg": {
        "pcg_actor_source_context": {
            "graphType": "pcg",
            "fixture": "pcg_graph_with_world_actor",
        },
    }
}

DEFAULT_FIXTURES: dict[str, dict[str, str]] = {
    "blueprint": {
        "construct_only": "blueprint_function_graph",
        "construct_and_query": "blueprint_function_graph",
        "read_write_roundtrip": "blueprint_function_graph",
        "dynamic_pin_probe": "blueprint_function_graph",
    },
    "material": {
        "construct_only": "material_graph",
        "construct_and_query": "material_graph",
        "read_write_roundtrip": "material_graph",
        "dynamic_pin_probe": "material_graph",
    },
    "pcg": {
        "construct_only": "pcg_graph",
        "construct_and_query": "pcg_graph",
        "read_write_roundtrip": "pcg_graph",
        "dynamic_pin_probe": "pcg_graph",
    }
}


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"expected object root in {path}")
    return value


def normalize_focus(testing: dict[str, Any]) -> dict[str, Any]:
    focus = testing.get("focus")
    if not isinstance(focus, dict):
        return {}

    normalized: dict[str, Any] = {}
    for key in ("fields", "dynamicTriggers", "selectorFields", "effectiveSettingsGroups", "workflowFamilies"):
        value = focus.get(key)
        if isinstance(value, list) and value:
            normalized[key] = value
    return normalized


def normalize_query_surface(testing: dict[str, Any]) -> dict[str, Any] | None:
    query_surface = testing.get("querySurface")
    if not isinstance(query_surface, dict):
        return None

    kind = query_surface.get("kind")
    if not isinstance(kind, str) or not kind:
        return None

    normalized: dict[str, Any] = {"kind": kind}
    groups = query_surface.get("groups")
    if isinstance(groups, list) and groups:
        normalized["groups"] = groups
    fallback = query_surface.get("fallback")
    if isinstance(fallback, str) and fallback:
        normalized["fallback"] = fallback
    return normalized


def build_entry(graph_type: str, node: dict[str, Any]) -> dict[str, Any]:
    class_name = node.get("className")
    display_name = node.get("displayName") or class_name
    family = node.get("family")
    testing = node.get("testing")
    if not isinstance(testing, dict):
        return {
            "nodeKey": class_name,
            "className": class_name,
            "displayName": display_name,
            "family": family,
            "profile": None,
            "querySurface": None,
            "mode": "blocked",
            "fixture": None,
            "recipe": None,
            "focus": {},
            "status": "blocked",
            "reason": "missing testing metadata",
        }

    profile = testing.get("profile")
    recipe = testing.get("recipe")
    focus = normalize_focus(testing)
    query_surface = normalize_query_surface(testing)
    reason = testing.get("reason")

    entry = {
        "nodeKey": class_name,
        "className": class_name,
        "displayName": display_name,
        "family": family,
        "profile": profile,
        "querySurface": query_surface,
        "mode": "blocked",
        "fixture": None,
        "recipe": recipe if isinstance(recipe, str) and recipe else None,
        "focus": focus,
        "status": "blocked",
    }
    if isinstance(reason, str) and reason:
        entry["reason"] = reason

    if not isinstance(profile, str) or not profile:
        entry["reason"] = "missing testing.profile"
        return entry

    if profile == "inventory_only":
        entry["mode"] = "inventory"
        entry["status"] = "inventory_only"
        return entry

    if profile == "semantic_family_represented":
        if focus.get("workflowFamilies"):
            entry["mode"] = "workflow_map"
            entry["status"] = "workflow_only"
        else:
            entry["reason"] = "missing focus.workflowFamilies"
        return entry

    recipe_bound = False
    if entry["recipe"]:
        recipe_def = RECIPE_REGISTRY.get(graph_type, {}).get(entry["recipe"])
        if not isinstance(recipe_def, dict):
            entry["reason"] = f"unknown recipe {entry['recipe']}"
            return entry
        fixture = recipe_def.get("fixture")
        if not isinstance(fixture, str) or fixture not in FIXTURE_REGISTRY.get(graph_type, {}):
            entry["reason"] = f"recipe fixture unavailable for {entry['recipe']}"
            return entry
        entry["fixture"] = fixture
        recipe_bound = True

    if profile == "context_recipe_required":
        if not recipe_bound:
            entry["reason"] = "missing recipe"
            return entry
        entry["mode"] = "recipe_case"
        entry["status"] = "ready"
        return entry

    if profile in AUTO_CASE_PROFILES:
        if not recipe_bound:
            fixture = DEFAULT_FIXTURES.get(graph_type, {}).get(profile)
            if not isinstance(fixture, str) or fixture not in FIXTURE_REGISTRY.get(graph_type, {}):
                entry["reason"] = f"missing fixture rule for profile {profile}"
                return entry
            entry["fixture"] = fixture
        if profile == "read_write_roundtrip" and not focus.get("fields"):
            entry["reason"] = "missing focus.fields"
            return entry
        if profile == "dynamic_pin_probe" and not focus.get("dynamicTriggers"):
            entry["reason"] = "missing focus.dynamicTriggers"
            return entry
        entry["mode"] = "recipe_case" if recipe_bound else "auto_case"
        entry["status"] = "ready"
        return entry

    entry["reason"] = f"unsupported testing.profile {profile}"
    return entry


def build_summary(entries: list[dict[str, Any]]) -> dict[str, int]:
    return {
        "totalNodes": len(entries),
        "readyAutoCases": sum(1 for entry in entries if entry["mode"] == "auto_case" and entry["status"] == "ready"),
        "readyRecipeCases": sum(1 for entry in entries if entry["mode"] == "recipe_case" and entry["status"] == "ready"),
        "workflowOnly": sum(1 for entry in entries if entry["status"] == "workflow_only"),
        "inventoryOnly": sum(1 for entry in entries if entry["status"] == "inventory_only"),
        "blocked": sum(1 for entry in entries if entry["status"] == "blocked"),
    }


def build_plan(graph_type: str, workspace_root: Path) -> dict[str, Any]:
    if graph_type not in SUPPORTED_GRAPH_TYPES:
        raise ValueError(f"unsupported graphType {graph_type}; supported={sorted(SUPPORTED_GRAPH_TYPES)}")

    source_catalog = workspace_root / graph_type / "catalogs" / "node-database.json"
    database = load_json(source_catalog)
    nodes = database.get("nodes")
    if not isinstance(nodes, list):
        raise ValueError(f"missing nodes[] in {source_catalog}")

    entries = sorted(
        (build_entry(graph_type, node) for node in nodes if isinstance(node, dict)),
        key=lambda entry: (entry["family"] or "", entry["className"] or ""),
    )
    return {
        "version": PLAN_VERSION,
        "graphType": graph_type,
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "sourceCatalog": {
            "path": str(source_catalog),
        },
        "summary": build_summary(entries),
        "entries": entries,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate first-version LOOMLE graph test plans.")
    parser.add_argument("--graph-type", choices=sorted(SUPPORTED_GRAPH_TYPES), required=True)
    parser.add_argument("--workspace-root", type=Path, default=WORKSPACE_ROOT)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    plan = build_plan(args.graph_type, args.workspace_root)
    text = json.dumps(plan, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
