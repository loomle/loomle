import type {
  Adapter,
  Binding,
  Blueprint,
  BlueprintComponent,
  BlueprintMember,
  Call,
  Condition,
  Expr,
  ObjectResult,
  Patch,
  Query,
  Value,
} from "../index.js";
import { validateQueryCapabilities } from "../core/capabilities.js";
import { isCall, isRef, symbolName } from "../core/expr.js";

export interface CreateMemoryBlueprintAdapterOptions {
  blueprints: Blueprint[];
}

export interface MemoryBlueprintAdapter extends Adapter {
  getBlueprints(): Blueprint[];
}

export function createMemoryBlueprintAdapter(
  options: CreateMemoryBlueprintAdapterOptions,
): MemoryBlueprintAdapter {
  const blueprints = options.blueprints.map(cloneBlueprint);

  return {
    domain: "blueprint",
    getBlueprints() {
      return blueprints.map(cloneBlueprint);
    },
    async query(query) {
      const target = query.target;
      if (target.domain !== "blueprint" || !("asset" in target)) {
        return { diagnostics: [diagnostic("invalid_blueprint_target", "Blueprint adapter requires a blueprint target.")] };
      }
      const blueprint = blueprints.find((candidate) => candidate.asset === target.asset);
      if (!blueprint) {
        return { diagnostics: [diagnostic("blueprint_not_found", `No in-memory blueprint is registered for ${target.asset}.`)] };
      }
      return executeBlueprintQuery(blueprint, query);
    },
    async patch(patch) {
      const target = patch.target;
      if (target.domain !== "blueprint" || !("asset" in target)) {
        return { diagnostics: [diagnostic("invalid_blueprint_target", "Blueprint adapter requires a blueprint target.")] };
      }
      const index = blueprints.findIndex((candidate) => candidate.asset === target.asset);
      if (index < 0) {
        return { diagnostics: [diagnostic("blueprint_not_found", `No in-memory blueprint is registered for ${target.asset}.`)] };
      }
      const result = planBlueprintPatch(blueprints[index], patch);
      if (result.diagnostics.length > 0 || !result.object || patch.dryRun) {
        return result;
      }
      if (result.object.kind !== "blueprint_result" || result.object.blueprints.length !== 1) {
        return { diagnostics: [diagnostic("patch_plan_failed", "Patch planning did not produce a blueprint.")] };
      }
      blueprints[index] = cloneBlueprint(result.object.blueprints[0]);
      return result;
    },
  };
}

function planBlueprintPatch(blueprint: Blueprint, patch: Patch): ObjectResult {
  const next = cloneBlueprint(blueprint);
  const bindings = new Map(patch.bindings.map((binding) => [bindingPath(binding), binding]));

  for (const op of patch.ops) {
    if (op.kind === "add" && "target" in op) {
      const key = op.target.path.join(".");
      const binding = bindings.get(key);
      if (!binding) {
        return { diagnostics: [diagnostic("unknown_blueprint_binding", `No binding is available for ${key}.`)] };
      }
      const addResult = applyAdd(next, op.target.path, binding);
      if (addResult) {
        return { diagnostics: [addResult] };
      }
      continue;
    }

    if (op.kind === "set" && "target" in op && "path" in op.target) {
      const setResult = applySet(next, op.target.path, op.value);
      if (setResult) {
        return { diagnostics: [setResult] };
      }
      continue;
    }

    if (op.kind === "remove" && "target" in op) {
      const removeResult = applyRemove(next, op.target.path);
      if (removeResult) {
        return { diagnostics: [removeResult] };
      }
      continue;
    }

    if (op.kind === "rename" && "target" in op) {
      const renameResult = applyRename(next, op.target.path, op.name);
      if (renameResult) {
        return { diagnostics: [renameResult] };
      }
      continue;
    }

    if (op.kind === "move" && "target" in op && "relativeTo" in op) {
      const moveResult = applyMove(next, op.target.path, op.relativeTo.path, op.position);
      if (moveResult) {
        return { diagnostics: [moveResult] };
      }
      continue;
    }

    return { diagnostics: [diagnostic("invalid_blueprint_patch_op", "Blueprint adapter can only execute blueprint add, set, remove, rename, and move operations.")] };
  }

  return { object: { kind: "blueprint_result", blueprints: [next] }, diagnostics: [] };
}

function applyAdd(blueprint: Blueprint, path: string[], binding: Binding): ObjectResult["diagnostics"][number] | undefined {
  if (!isCall(binding.value)) {
    return diagnostic("invalid_blueprint_binding", "Blueprint add requires a constructor binding.");
  }

  if (isMemberCall(binding.value.callee)) {
    if (path.length !== 2 || isComponentName(blueprint, path[0])) {
      return diagnostic("invalid_blueprint_member_target", "Blueprint member add target must be blueprint.member.");
    }
    if ((blueprint.members ?? []).some((member) => member.name === path[1])) {
      return diagnostic("duplicate_blueprint_member", `Blueprint member ${path[1]} already exists.`);
    }
    blueprint.members = [...(blueprint.members ?? []), memberFromCall(path[1], binding.value)];
    return undefined;
  }

  if (binding.value.callee === "component") {
    if (path.length !== 2) {
      return diagnostic("invalid_blueprint_component_target", "Blueprint component add target must be owner.component.");
    }
    if (typeof binding.value.args.class !== "string") {
      return diagnostic("invalid_blueprint_binding", "component(...) requires class: string.");
    }
    if ((blueprint.components ?? []).some((component) => component.name === path[1])) {
      return diagnostic("duplicate_blueprint_component", `Blueprint component ${path[1]} already exists.`);
    }
    const parent = isComponentName(blueprint, path[0]) ? path[0] : null;
    if (parent && !(blueprint.components ?? []).some((component) => component.name === parent)) {
      return diagnostic("unknown_component_parent", `Component parent ${parent} does not exist.`);
    }
    blueprint.components = [...(blueprint.components ?? []), componentFromCall(path[1], binding.value, parent)];
    return undefined;
  }

  return diagnostic("invalid_blueprint_binding", `Unsupported Blueprint constructor ${binding.value.callee}.`);
}

function applySet(blueprint: Blueprint, path: string[], value: Expr): ObjectResult["diagnostics"][number] | undefined {
  if (path.length === 2 && !isComponentName(blueprint, path[0])) {
    return setBlueprintField(blueprint, path[1], value);
  }

  if (path.length === 3 && !isComponentName(blueprint, path[0])) {
    const member = (blueprint.members ?? []).find((candidate) => candidate.name === path[1]);
    if (member) {
      return setMemberField(member, path[2], value);
    }
    const component = (blueprint.components ?? []).find((candidate) => candidate.name === path[1]);
    if (component) {
      return setComponentField(component, path[2], value);
    }
  }

  if (path.length === 2) {
    const component = (blueprint.components ?? []).find((candidate) => candidate.name === path[0]);
    if (component) {
      return setComponentField(component, path[1], value);
    }
  }

  return diagnostic("unknown_blueprint_set_target", `Unknown Blueprint set target ${path.join(".")}.`);
}

function applyRemove(blueprint: Blueprint, path: string[]): ObjectResult["diagnostics"][number] | undefined {
  if (path.length !== 2) {
    return diagnostic("invalid_blueprint_remove_target", "Blueprint remove target must be owner.name.");
  }

  if (!isComponentName(blueprint, path[0])) {
    const memberCount = blueprint.members?.length ?? 0;
    blueprint.members = (blueprint.members ?? []).filter((member) => member.name !== path[1]);
    if ((blueprint.members?.length ?? 0) !== memberCount) {
      return undefined;
    }
  }

  const componentCount = blueprint.components?.length ?? 0;
  blueprint.components = removeComponentTree(blueprint.components ?? [], path[1]);
  if ((blueprint.components?.length ?? 0) !== componentCount) {
    return undefined;
  }

  return diagnostic("unknown_blueprint_remove_target", `Unknown Blueprint remove target ${path.join(".")}.`);
}

function applyRename(blueprint: Blueprint, path: string[], name: string): ObjectResult["diagnostics"][number] | undefined {
  if (path.length !== 2) {
    return diagnostic("invalid_blueprint_rename_target", "Blueprint rename target must be owner.name.");
  }

  if (!isComponentName(blueprint, path[0])) {
    const member = (blueprint.members ?? []).find((candidate) => candidate.name === path[1]);
    if (member) {
      if ((blueprint.members ?? []).some((candidate) => candidate.name === name)) {
        return diagnostic("duplicate_blueprint_member", `Blueprint member ${name} already exists.`);
      }
      member.name = name;
      return undefined;
    }
  }

  const component = (blueprint.components ?? []).find((candidate) => candidate.name === path[1]);
  if (component) {
    if ((blueprint.components ?? []).some((candidate) => candidate.name === name)) {
      return diagnostic("duplicate_blueprint_component", `Blueprint component ${name} already exists.`);
    }
    for (const child of blueprint.components ?? []) {
      if (child.parent === component.name) {
        child.parent = name;
      }
    }
    component.name = name;
    return undefined;
  }

  return diagnostic("unknown_blueprint_rename_target", `Unknown Blueprint rename target ${path.join(".")}.`);
}

function applyMove(
  blueprint: Blueprint,
  targetPath: string[],
  relativeToPath: string[],
  position: "before" | "after",
): ObjectResult["diagnostics"][number] | undefined {
  const targetName = targetPath[targetPath.length - 1];
  const relativeName = relativeToPath[relativeToPath.length - 1];
  const components = blueprint.components ?? [];
  const targetIndex = components.findIndex((component) => component.name === targetName);
  const relativeIndex = components.findIndex((component) => component.name === relativeName);
  if (targetIndex < 0) {
    return diagnostic("unknown_component", `Component ${targetName} does not exist.`);
  }
  if (relativeIndex < 0) {
    return diagnostic("unknown_component", `Component ${relativeName} does not exist.`);
  }
  if (components[targetIndex].parent !== components[relativeIndex].parent) {
    return diagnostic("invalid_component_move", "Component move requires both components to share the same parent.");
  }

  const [component] = components.splice(targetIndex, 1);
  const adjustedRelativeIndex = components.findIndex((candidate) => candidate.name === relativeName);
  components.splice(position === "before" ? adjustedRelativeIndex : adjustedRelativeIndex + 1, 0, component);
  blueprint.components = components;
  return undefined;
}

function isComponentName(blueprint: Blueprint, name: string): boolean {
  return (blueprint.components ?? []).some((component) => component.name === name);
}

function executeBlueprintQuery(blueprint: Blueprint, query: Query): ObjectResult {
  const capabilityDiagnostics = validateQueryCapabilities(query, {
    domain: "blueprint",
    findKinds: ["members", "components"],
    whereFields: ["name", "kind", "type", "category", "class", "parent"],
    orderKeys: ["name", "kind", "type", "category", "class", "parent"],
    supportsPageAfter: true,
    supportsCompare: true,
  });
  if (capabilityDiagnostics.length > 0) {
    return { diagnostics: capabilityDiagnostics };
  }

  const find = query.find;
  if (!find) {
    return { object: { kind: "blueprint_result", blueprints: [cloneBlueprint(blueprint)] }, diagnostics: [] };
  }
  if (find.kind === "members") {
    const members = (blueprint.members ?? [])
      .filter((member) => matchesText(member.name, find.text))
      .filter((member) => matchesMemberCondition(member, query.where));
    const page = paginateItems(sortItems(members, query, (member, key) => readMemberField(member, key.split("."))), query);
    return {
      object: {
        kind: "blueprint_result",
        blueprints: [{ ...baseBlueprint(blueprint), members: page.items.map(cloneMember) }],
      },
      diagnostics: [],
      ...(page.next ? { page: { next: page.next } } : {}),
    };
  }
  if (find.kind === "components") {
    const components = (blueprint.components ?? [])
      .filter((component) => matchesText(component.name, find.text) || matchesText(component.class, find.text))
      .filter((component) => matchesComponentCondition(component, query.where));
    const page = paginateItems(sortItems(components, query, (component, key) => readComponentField(component, key.split("."))), query);
    return {
      object: {
        kind: "blueprint_result",
        blueprints: [{ ...baseBlueprint(blueprint), components: page.items.map(cloneComponent) }],
      },
      diagnostics: [],
      ...(page.next ? { page: { next: page.next } } : {}),
    };
  }
  return { diagnostics: [diagnostic("invalid_blueprint_find", "Blueprint adapter can only execute member and component queries.")] };
}

function isMemberCall(callee: string): boolean {
  return ["variable", "function", "macro", "dispatcher", "event"].includes(callee);
}

function setBlueprintField(blueprint: Blueprint, field: string, value: Expr): ObjectResult["diagnostics"][number] | undefined {
  switch (field) {
    case "parent":
    case "namespace":
    case "category": {
      const stringValue = exprToString(value);
      if (stringValue === undefined) {
        return diagnostic("invalid_blueprint_value", `Blueprint field ${field} requires a string or symbol value.`);
      }
      blueprint[field] = stringValue;
      return undefined;
    }
    case "abstract":
    case "deprecated":
      if (typeof value !== "boolean") {
        return diagnostic("invalid_blueprint_value", `Blueprint field ${field} requires a boolean value.`);
      }
      blueprint[field] = value;
      return undefined;
    default:
      return diagnostic("unsupported_blueprint_field", `Unsupported Blueprint field ${field}.`);
  }
}

function setMemberField(member: BlueprintMember, field: string, value: Expr): ObjectResult["diagnostics"][number] | undefined {
  switch (field) {
    case "type":
    case "category":
    case "replication": {
      const stringValue = exprToString(value);
      if (stringValue === undefined) {
        return diagnostic("invalid_member_value", `Member field ${field} requires a string or symbol value.`);
      }
      member[field] = stringValue;
      return undefined;
    }
    case "default":
      if (!isValue(value)) {
        return diagnostic("invalid_member_value", "Member default requires a literal value.");
      }
      member.default = value;
      return undefined;
    case "pure":
    case "const":
    case "reliable":
      if (typeof value !== "boolean") {
        return diagnostic("invalid_member_value", `Member field ${field} requires a boolean value.`);
      }
      member[field] = value;
      return undefined;
    case "metadata":
      if (!isValueRecord(value)) {
        return diagnostic("invalid_member_value", "Member metadata requires an object value.");
      }
      member.metadata = value;
      return undefined;
    default:
      return diagnostic("unsupported_member_field", `Unsupported member field ${field}.`);
  }
}

function setComponentField(component: BlueprintComponent, field: string, value: Expr): ObjectResult["diagnostics"][number] | undefined {
  if (field === "class") {
    if (typeof value !== "string") {
      return diagnostic("invalid_component_value", "Component class requires a string value.");
    }
    component.class = value;
    return undefined;
  }
  if (!isValue(value)) {
    return diagnostic("invalid_component_value", "Component property values must be literal values.");
  }
  component.properties = { ...(component.properties ?? {}), [field]: value };
  return undefined;
}

function memberFromCall(name: string, call: Call): BlueprintMember {
  return {
    kind: call.callee as BlueprintMember["kind"],
    name,
    ...(symbolName(call.args.type) ? { type: symbolName(call.args.type) } : {}),
    ...(call.args.default !== undefined && isValue(call.args.default) ? { default: call.args.default } : {}),
    ...(typeof call.args.category === "string" ? { category: call.args.category } : {}),
    ...(stringMap(call.args.inputs) ? { inputs: stringMap(call.args.inputs) } : {}),
    ...(stringMap(call.args.outputs) ? { outputs: stringMap(call.args.outputs) } : {}),
    ...(typeof call.args.pure === "boolean" ? { pure: call.args.pure } : {}),
    ...(typeof call.args.const === "boolean" ? { const: call.args.const } : {}),
    ...(symbolName(call.args.replication) ? { replication: symbolName(call.args.replication) } : {}),
    ...(typeof call.args.reliable === "boolean" ? { reliable: call.args.reliable } : {}),
    ...(isValueRecord(call.args.metadata) ? { metadata: call.args.metadata } : {}),
  };
}

function componentFromCall(name: string, call: Call, parent: string | null): BlueprintComponent {
  const classPath = call.args.class;
  const { class: _class, parent: _parent, ...properties } = call.args;
  return {
    name,
    class: classPath as string,
    parent,
    ...(isValueRecord(properties) && Object.keys(properties).length > 0 ? { properties } : {}),
  };
}

function removeComponentTree(components: BlueprintComponent[], name: string): BlueprintComponent[] {
  const toRemove = new Set([name]);
  let changed = true;
  while (changed) {
    changed = false;
    for (const component of components) {
      if (component.parent && toRemove.has(component.parent) && !toRemove.has(component.name)) {
        toRemove.add(component.name);
        changed = true;
      }
    }
  }
  return components.filter((component) => !toRemove.has(component.name));
}

function baseBlueprint(blueprint: Blueprint): Blueprint {
  const { members: _members, components: _components, ...base } = blueprint;
  return cloneBlueprint(base);
}

function bindingPath(binding: Binding): string {
  switch (binding.target.kind) {
    case "local":
      return binding.target.name;
    case "member":
      return `${binding.target.object}.${binding.target.member}`;
    default:
      return assertNever(binding.target);
  }
}

function exprToString(value: Expr): string | undefined {
  if (typeof value === "string") {
    return value;
  }
  if (isName(value)) {
    return value.name;
  }
  return undefined;
}

function stringMap(value: Expr | undefined): Record<string, string> | undefined {
  if (!isValueRecord(value)) {
    return undefined;
  }
  const entries = Object.entries(value);
  if (!entries.every(([, item]) => typeof item === "string" || isName(item))) {
    return undefined;
  }
  return Object.fromEntries(entries.map(([key, item]) => [key, typeof item === "string" ? item : (item as { kind: "name"; name: string }).name]));
}

function isValue(value: Expr): value is Value {
  return !isCall(value) && !isRef(value);
}

function isValueRecord(value: unknown): value is Record<string, Value> {
  return (
    typeof value === "object" &&
    value !== null &&
    !Array.isArray(value) &&
    !isCall(value) &&
    !isRef(value)
  );
}

function matchesText(value: string, text: string | undefined): boolean {
  return !text || value.toLowerCase().includes(text.toLowerCase());
}

function matchesMemberCondition(member: BlueprintMember, condition: Condition | undefined): boolean {
  return matchesCondition(condition, (path) => readMemberField(member, path));
}

function matchesComponentCondition(component: BlueprintComponent, condition: Condition | undefined): boolean {
  return matchesCondition(condition, (path) => readComponentField(component, path));
}

function matchesCondition(condition: Condition | undefined, readField: (path: string[]) => unknown): boolean {
  if (!condition) {
    return true;
  }
  switch (condition.kind) {
    case "eq":
      return String(readField(condition.field.path)) === exprToConditionString(condition.value);
    case "ne":
      return String(readField(condition.field.path)) !== exprToConditionString(condition.value);
    case "contains":
      return String(readField(condition.field.path)).includes(exprToConditionString(condition.value));
    case "compare":
      return compareValues(readField(condition.field.path), condition.op, condition.value);
    case "not":
      return !matchesCondition(condition.condition, readField);
    case "and":
      return condition.conditions.every((item) => matchesCondition(item, readField));
    case "or":
      return condition.conditions.some((item) => matchesCondition(item, readField));
    default:
      return assertNever(condition);
  }
}

function readMemberField(member: BlueprintMember, path: string[]): unknown {
  const field = path.join(".");
  if (field === "name") {
    return member.name;
  }
  if (field === "kind") {
    return member.kind;
  }
  if (field === "type") {
    return member.type;
  }
  if (field === "category") {
    return member.category;
  }
  return undefined;
}

function readComponentField(component: BlueprintComponent, path: string[]): unknown {
  const field = path.join(".");
  if (field === "name") {
    return component.name;
  }
  if (field === "class" || field === "type") {
    return component.class;
  }
  if (field === "parent") {
    return component.parent;
  }
  return undefined;
}

function sortItems<T>(items: T[], query: Query, readField: (item: T, key: string) => unknown): T[] {
  if (!query.orderBy || query.orderBy.length === 0) {
    return items;
  }
  return [...items].sort((left, right) => {
    for (const order of query.orderBy ?? []) {
      const result = compareSortable(readField(left, order.key), readField(right, order.key));
      if (result !== 0) {
        return order.direction === "desc" ? -result : result;
      }
    }
    return 0;
  });
}

function paginateItems<T>(items: T[], query: Query): { items: T[]; next?: string } {
  const start = query.page?.after ? cursorOffset(query.page.after) : 0;
  const limit = query.page?.limit ?? 50;
  const end = start + limit;
  return {
    items: items.slice(start, end),
    ...(end < items.length ? { next: `offset:${end}` } : {}),
  };
}

function cursorOffset(cursor: string): number {
  const match = /^offset:(\d+)$/.exec(cursor);
  return match ? Number(match[1]) : 0;
}

function compareSortable(left: unknown, right: unknown): number {
  return String(left ?? "").localeCompare(String(right ?? ""));
}

function compareValues(left: unknown, op: "gt" | "gte" | "lt" | "lte", right: Expr): boolean {
  const leftNumber = typeof left === "number" ? left : Number(left);
  const rightNumber = Number(exprToConditionString(right));
  if (Number.isNaN(leftNumber) || Number.isNaN(rightNumber)) {
    return false;
  }
  switch (op) {
    case "gt":
      return leftNumber > rightNumber;
    case "gte":
      return leftNumber >= rightNumber;
    case "lt":
      return leftNumber < rightNumber;
    case "lte":
      return leftNumber <= rightNumber;
    default:
      return assertNever(op);
  }
}

function exprToConditionString(value: Expr): string {
  if (isName(value)) {
    return value.name;
  }
  if (isRef(value)) {
    return value.kind === "member" ? `${value.object}.${value.member}` : value.kind === "id" ? value.id : value.name;
  }
  return String(value);
}

function isName(value: Expr): value is { kind: "name"; name: string } {
  return typeof value === "object" && value !== null && !Array.isArray(value) && value.kind === "name";
}

function cloneBlueprint(blueprint: Blueprint): Blueprint {
  return structuredClone(blueprint);
}

function cloneMember(member: BlueprintMember): BlueprintMember {
  return structuredClone(member);
}

function cloneComponent(component: BlueprintComponent): BlueprintComponent {
  return structuredClone(component);
}

function diagnostic(code: string, message: string): ObjectResult["diagnostics"][number] {
  return { severity: "error", code, message };
}

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}
