import type {
  Adapter,
  Binding,
  Call,
  Condition,
  Expr,
  ObjectResult,
  Patch,
  Query,
  Value,
  WidgetDocument,
  WidgetNode,
} from "../index.js";
import { isCall, isRef } from "../core/expr.js";

export interface CreateMemoryWidgetAdapterOptions {
  documents: WidgetDocument[];
}

export interface MemoryWidgetAdapter extends Adapter {
  getDocuments(): WidgetDocument[];
}

export function createMemoryWidgetAdapter(
  options: CreateMemoryWidgetAdapterOptions,
): MemoryWidgetAdapter {
  const documents = options.documents.map(cloneDocument);

  return {
    domain: "widget",
    getDocuments() {
      return documents.map(cloneDocument);
    },
    async query(query) {
      const target = query.target;
      if (target.domain !== "widget" || !("asset" in target)) {
        return { diagnostics: [diagnostic("invalid_widget_target", "Widget adapter requires a widget target.")] };
      }
      const document = documents.find((candidate) => candidate.asset === target.asset);
      if (!document) {
        return { diagnostics: [diagnostic("widget_not_found", `No in-memory widget document is registered for ${target.asset}.`)] };
      }
      return executeWidgetQuery(document, query);
    },
    async patch(patch) {
      const target = patch.target;
      if (target.domain !== "widget" || !("asset" in target)) {
        return { diagnostics: [diagnostic("invalid_widget_target", "Widget adapter requires a widget target.")] };
      }
      const index = documents.findIndex((candidate) => candidate.asset === target.asset);
      if (index < 0) {
        return { diagnostics: [diagnostic("widget_not_found", `No in-memory widget document is registered for ${target.asset}.`)] };
      }
      const result = planWidgetPatch(documents[index], patch);
      if (result.diagnostics.length > 0 || !result.object || patch.dryRun) {
        return result;
      }
      if (result.object.kind !== "widget_result" || result.object.documents.length !== 1) {
        return { diagnostics: [diagnostic("patch_plan_failed", "Patch planning did not produce a widget document.")] };
      }
      documents[index] = cloneDocument(result.object.documents[0]);
      return result;
    },
  };
}

function planWidgetPatch(document: WidgetDocument, patch: Patch): ObjectResult {
  const next = cloneDocument(document);
  const bindings = new Map(patch.bindings.map((binding) => [bindingPath(binding), binding]));

  for (const op of patch.ops) {
    if (op.kind === "add" && "target" in op && "path" in op.target) {
      const key = op.target.path.join(".");
      const binding = bindings.get(key);
      if (!binding) {
        return { diagnostics: [diagnostic("unknown_widget_binding", `No binding is available for ${key}.`)] };
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

    if (op.kind === "remove" && "target" in op && "path" in op.target) {
      const removeResult = applyRemove(next, op.target.path);
      if (removeResult) {
        return { diagnostics: [removeResult] };
      }
      continue;
    }

    return { diagnostics: [diagnostic("invalid_widget_patch_op", "Widget adapter can only execute widget add, set, and remove operations.")] };
  }

  return { object: { kind: "widget_result", documents: [next] }, diagnostics: [] };
}

function applyAdd(document: WidgetDocument, path: string[], binding: Binding): ObjectResult["diagnostics"][number] | undefined {
  if (path.length !== 2) {
    return diagnostic("invalid_widget_add_target", "Widget add target must be parent.child.");
  }
  if (!isCall(binding.value)) {
    return diagnostic("invalid_widget_binding", "Widget add requires a constructor binding.");
  }
  if (!document.widgets.some((widget) => widget.alias === path[0])) {
    return diagnostic("unknown_widget_parent", `Widget parent ${path[0]} does not exist.`);
  }
  if (document.widgets.some((widget) => widget.alias === path[1])) {
    return diagnostic("duplicate_widget", `Widget ${path[1]} already exists.`);
  }
  const node = nodeFromCall(path[1], binding.value, path[0]);
  if (!node) {
    return diagnostic("invalid_widget_binding", "Widget constructor properties must be literal values.");
  }
  document.widgets.push(node);
  return undefined;
}

function applySet(document: WidgetDocument, path: string[], value: Expr): ObjectResult["diagnostics"][number] | undefined {
  if (path.length !== 2) {
    return diagnostic("invalid_widget_set_target", "Widget set target must be widget.property.");
  }
  const widget = document.widgets.find((candidate) => candidate.alias === path[0]);
  if (!widget) {
    return diagnostic("unknown_widget", `Widget ${path[0]} does not exist.`);
  }
  if (!isValue(value)) {
    return diagnostic("invalid_widget_value", "Widget property values must be literal values.");
  }
  widget.properties = { ...(widget.properties ?? {}), [path[1]]: value };
  return undefined;
}

function applyRemove(document: WidgetDocument, path: string[]): ObjectResult["diagnostics"][number] | undefined {
  if (path.length !== 1) {
    return diagnostic("invalid_widget_remove_target", "Widget remove target must be a widget name.");
  }
  if (path[0] === document.root) {
    return diagnostic("invalid_widget_remove_target", "Widget root cannot be removed.");
  }
  const before = document.widgets.length;
  document.widgets = removeWidgetTree(document.widgets, path[0]);
  return document.widgets.length === before
    ? diagnostic("unknown_widget", `Widget ${path[0]} does not exist.`)
    : undefined;
}

function executeWidgetQuery(document: WidgetDocument, query: Query): ObjectResult {
  const find = query.find;
  if (!find || find.kind === "tree") {
    return { object: { kind: "widget_result", documents: [cloneDocument(document)] }, diagnostics: [] };
  }
  if (find.kind === "widgets") {
    const widgets = document.widgets
      .filter((widget) => matchesWidgetText(widget, find.text))
      .filter((widget) => matchesWidgetCondition(widget, query.where));
    const page = paginateItems(sortItems(widgets, query, (widget, key) => readWidgetField(widget, key.split("."))), query);
    return {
      object: {
        kind: "widget_result",
        documents: [{ ...baseDocument(document), widgets: page.items.map(cloneWidget) }],
      },
      diagnostics: [],
      ...(page.next ? { page: { next: page.next } } : {}),
    };
  }
  return { diagnostics: [diagnostic("invalid_widget_find", "Widget adapter can only execute find tree and find widgets queries.")] };
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

function nodeFromCall(alias: string, call: Call, parent: string): WidgetNode | undefined {
  const { parent: _parent, ...properties } = call.args;
  if (!isValueRecord(properties)) {
    return undefined;
  }
  return {
    alias,
    class: call.callee,
    parent,
    ...(Object.keys(properties).length > 0 ? { properties } : {}),
  };
}

function removeWidgetTree(widgets: WidgetNode[], alias: string): WidgetNode[] {
  const toRemove = new Set([alias]);
  let changed = true;
  while (changed) {
    changed = false;
    for (const widget of widgets) {
      if (widget.parent && toRemove.has(widget.parent) && !toRemove.has(widget.alias)) {
        toRemove.add(widget.alias);
        changed = true;
      }
    }
  }
  return widgets.filter((widget) => !toRemove.has(widget.alias));
}

function baseDocument(document: WidgetDocument): WidgetDocument {
  const { widgets: _widgets, ...base } = document;
  return { ...base, widgets: [] };
}

function matchesWidgetText(widget: WidgetNode, text: string | undefined): boolean {
  if (!text) {
    return true;
  }
  const lowered = text.toLowerCase();
  return [widget.alias, widget.class, ...Object.values(widget.properties ?? {}).map(String)].some((field) =>
    field.toLowerCase().includes(lowered),
  );
}

function matchesWidgetCondition(widget: WidgetNode, condition: Condition | undefined): boolean {
  return matchesCondition(condition, (path) => readWidgetField(widget, path));
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

function readWidgetField(widget: WidgetNode, path: string[]): unknown {
  const field = path.join(".");
  if (field === "name" || field === "alias") {
    return widget.alias;
  }
  if (field === "class" || field === "type") {
    return widget.class;
  }
  if (field === "parent") {
    return widget.parent;
  }
  if (path[0] === "property" && path[1]) {
    return widget.properties?.[path[1]];
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

function isValue(value: Expr): value is Value {
  return !isCall(value) && !isRef(value);
}

function isValueRecord(value: unknown): value is Record<string, Value> {
  return (
    typeof value === "object" &&
    value !== null &&
    !Array.isArray(value) &&
    !isCall(value) &&
    !isRef(value) &&
    Object.values(value).every(isValue)
  );
}

function cloneDocument(document: WidgetDocument): WidgetDocument {
  return structuredClone(document);
}

function cloneWidget(widget: WidgetNode): WidgetNode {
  return structuredClone(widget);
}

function diagnostic(code: string, message: string): ObjectResult["diagnostics"][number] {
  return { severity: "error", code, message };
}

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}
