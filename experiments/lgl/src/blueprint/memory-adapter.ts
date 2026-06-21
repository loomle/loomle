import type {
  Adapter,
  Blueprint,
  BlueprintComponent,
  BlueprintMember,
  Condition,
  Expr,
  ObjectResult,
  Query,
} from "../index.js";
import { isRef } from "../core/expr.js";

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
  };
}

function executeBlueprintQuery(blueprint: Blueprint, query: Query): ObjectResult {
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

function baseBlueprint(blueprint: Blueprint): Blueprint {
  const { members: _members, components: _components, ...base } = blueprint;
  return cloneBlueprint(base);
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
