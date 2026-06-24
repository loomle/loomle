import type { CreationEntry, Expr, LglObject, Property, Query, Value, WidgetDocument, WidgetNode, WidgetResult } from "../index.js";
import { tryParseBinding } from "../core/binding.js";
import { parseCondition, parseDetails, parseOrderBy, parsePage } from "../core/condition.js";
import { isCall, isLocalRef, symbolName } from "../core/expr.js";
import { ParseError, type ParsedLine, spanForLine } from "../core/text.js";

export interface WidgetBinding {
  alias: string;
  asset: string;
  root: string;
}

export function parseWidgetBindings(lines: ParsedLine[]): Map<string, WidgetBinding> {
  const assets = new Map<string, string>();
  const widgets = new Map<string, WidgetBinding>();

  for (const line of lines) {
    if (line.text.startsWith("query ") || line.text.startsWith("patch ")) {
      break;
    }
    const binding = tryParseBinding(line);
    if (!binding || binding.target.kind !== "local" || !isCall(binding.value)) {
      continue;
    }
    if (binding.value.callee === "asset" && typeof binding.value.args.path === "string") {
      assets.set(binding.target.name, binding.value.args.path);
      continue;
    }
    if (binding.value.callee === "widget" && ("asset" in binding.value.args || "root" in binding.value.args)) {
      const asset = resolveAsset(binding.value.args.asset, assets, line);
      const root = binding.value.args.root;
      if (!isLocalRef(root) && typeof root !== "string") {
        throw new ParseError("invalid_widget_binding", "widget(...) requires root: widget alias.", spanForLine(line));
      }
      widgets.set(binding.target.name, {
        alias: binding.target.name,
        asset,
        root: isLocalRef(root) ? root.name : root,
      });
    }
  }

  return widgets;
}

export function parseWidgetQuery(
  lines: ParsedLine[],
  queryIndex: number,
  bindings: Map<string, WidgetBinding>,
): Query {
  const queryLine = lines[queryIndex];
  const match = /^query\s+([A-Za-z_][A-Za-z0-9_]*)$/.exec(queryLine.text);
  if (!match) {
    throw new ParseError("invalid_query", "Expected query <widget>.", spanForLine(queryLine));
  }
  const binding = bindings.get(match[1]);
  if (!binding) {
    throw new ParseError("unknown_widget_binding", `Unknown widget binding ${match[1]}.`, spanForLine(queryLine));
  }

  const query: Query = {
    kind: "query",
    target: { domain: "widget", asset: binding.asset },
  };

  for (const line of lines.slice(queryIndex + 1)) {
    if (line.text.startsWith("find ")) {
      query.find = parseWidgetFind(line);
    } else if (line.text.startsWith("where ")) {
      query.where = parseCondition(line.text.slice("where ".length), line);
    } else if (line.text.startsWith("with ")) {
      query.with = parseDetails(line.text.slice("with ".length));
    } else if (line.text.startsWith("order by ")) {
      query.orderBy = parseOrderBy(line.text.slice("order by ".length));
    } else if (line.text.startsWith("page ")) {
      query.page = { ...(query.page ?? {}), ...parsePage(line) };
    } else {
      throw new ParseError("unsupported_query_clause", "Unsupported widget query clause.", spanForLine(line));
    }
  }

  return query;
}

function parseWidgetFind(line: ParsedLine): Query["find"] {
  if (line.text === "find tree") {
    return { kind: "tree" };
  }
  const match = /^find widgets(?:\s+"([^"]+)")?$/.exec(line.text);
  if (match) {
    return { kind: "widgets", ...(match[1] ? { text: match[1] } : {}) };
  }
  const paletteMatch = /^find palette entry(?:\s+"([^"]+)")?$/.exec(line.text);
  if (paletteMatch) {
    return { kind: "palette_entry", ...(paletteMatch[1] ? { text: paletteMatch[1] } : {}) };
  }
  throw new ParseError("unsupported_widget_query", "Expected find tree, find widgets [\"text\"], or find palette entry [\"text\"].", spanForLine(line));
}

export function tryParseWidgetCreationResult(
  lines: ParsedLine[],
  bindings: Map<string, WidgetBinding>,
): LglObject | undefined {
  const entries: CreationEntry[] = [];
  const propertiesByEntry = new Map<string, Property[]>();
  const targetBinding = bindings.values().next().value as WidgetBinding | undefined;
  if (!targetBinding) {
    return undefined;
  }

  for (const line of lines) {
    const binding = tryParseBinding(line);
    if (!binding || !isCall(binding.value)) {
      continue;
    }
    if (binding.target.kind === "member" && binding.value.callee === "property") {
      const properties = propertiesByEntry.get(binding.target.object) ?? [];
      properties.push(propertyFromBinding(binding.target.member, binding.value.args, line));
      propertiesByEntry.set(binding.target.object, properties);
      continue;
    }
    if (binding.target.kind !== "local") {
      continue;
    }
    if (binding.value.callee === "asset" || bindings.has(binding.target.name)) {
      continue;
    }
    if (binding.value.callee === "widget" && typeof binding.value.args.class === "string") {
      entries.push({
        name: binding.target.name,
        class: binding.value.args.class,
      });
      continue;
    }
    if (binding.value.callee === "widget" && typeof binding.value.args.palette === "string") {
      entries.push({
        name: binding.target.name,
        palette: { kind: "palette", id: binding.value.args.palette },
      });
      continue;
    }
    entries.push({
      name: binding.target.name,
      constructor: binding.value,
    });
  }

  for (const entry of entries) {
    const properties = propertiesByEntry.get(entry.name);
    if (properties && properties.length > 0) {
      entry.properties = properties;
    }
  }

  return entries.length > 0
    ? {
        kind: "creation_result",
        target: { domain: "widget", asset: targetBinding.asset },
        entries,
      }
    : undefined;
}

function propertyFromBinding(name: string, args: Record<string, Expr>, line: ParsedLine): Property {
  const type = symbolName(args.type);
  if (!type) {
    throw new ParseError("invalid_widget_property", "property(...) requires type: symbol.", spanForLine(line));
  }
  const writable = args.writable;
  if (writable !== undefined && typeof writable !== "boolean") {
    throw new ParseError("invalid_widget_property", "property(...) writable must be boolean.", spanForLine(line));
  }
  const category = args.category;
  if (category !== undefined && typeof category !== "string") {
    throw new ParseError("invalid_widget_property", "property(...) category must be string.", spanForLine(line));
  }
  return {
    name,
    type,
    ...(args.default !== undefined ? { default: args.default } : {}),
    ...(writable !== undefined ? { writable } : {}),
    ...(category !== undefined ? { category } : {}),
  };
}

export function tryParseWidgetResult(lines: ParsedLine[]): WidgetResult | undefined {
  const assets = new Map<string, string>();
  const documents = new Map<string, WidgetDocument>();
  const nodeOwners = new Map<string, WidgetDocument>();

  for (const line of lines) {
    const binding = tryParseBinding(line);
    if (!binding || !isCall(binding.value)) {
      return undefined;
    }

    if (binding.target.kind === "local" && binding.value.callee === "asset") {
      if (typeof binding.value.args.path === "string") {
        assets.set(binding.target.name, binding.value.args.path);
      }
      continue;
    }

    if (binding.target.kind === "local" && binding.value.callee === "widget") {
      const document = widgetDocumentFromBinding(binding.target.name, binding.value.args, assets, line);
      documents.set(document.alias, document);
      continue;
    }

    if (binding.target.kind === "local") {
      const document = findDocumentForRoot(binding.target.name, documents);
      if (!document) {
        return undefined;
      }
      const node = nodeFromBinding(binding.target.name, binding.value, undefined, line);
      document.widgets.push(node);
      nodeOwners.set(node.alias, document);
      continue;
    }

    if (binding.target.kind === "member") {
      const document = nodeOwners.get(binding.target.object) ?? findDocumentForRoot(binding.target.object, documents);
      if (!document) {
        return undefined;
      }
      const node = nodeFromBinding(binding.target.member, binding.value, binding.target.object, line);
      document.widgets.push(node);
      nodeOwners.set(node.alias, document);
      continue;
    }
  }

  return documents.size > 0 ? { kind: "widget_result", documents: [...documents.values()] } : undefined;
}

function widgetDocumentFromBinding(
  alias: string,
  args: Record<string, Expr>,
  assets: Map<string, string>,
  line: ParsedLine,
): WidgetDocument {
  const asset = resolveAsset(args.asset, assets, line);
  const root = args.root;
  if (!isLocalRef(root) && typeof root !== "string") {
    throw new ParseError("invalid_widget_binding", "widget(...) requires root.", spanForLine(line));
  }
  return {
    alias,
    asset,
    root: isLocalRef(root) ? root.name : root,
    widgets: [],
  };
}

function resolveAsset(expr: Expr | undefined, assets: Map<string, string>, line: ParsedLine): string {
  if (typeof expr === "string") {
    return expr;
  }
  if (!isLocalRef(expr)) {
    throw new ParseError("invalid_widget_binding", "widget(...) requires asset: assetBinding or asset path.", spanForLine(line));
  }
  const asset = assets.get(expr.name);
  if (!asset) {
    throw new ParseError("unknown_asset_binding", `Unknown asset binding ${expr.name}.`, spanForLine(line));
  }
  return asset;
}

function findDocumentForRoot(root: string, documents: Map<string, WidgetDocument>): WidgetDocument | undefined {
  for (const document of documents.values()) {
    if (document.root === root) {
      return document;
    }
  }
  return undefined;
}

function nodeFromBinding(alias: string, call: { callee: string; args: Record<string, Expr> }, parent: string | undefined, line: ParsedLine): WidgetNode {
  const { parent: _parent, ...properties } = call.args;
  if (Object.keys(properties).length > 0 && !isValueRecord(properties)) {
    throw new ParseError("invalid_widget_binding", "Widget properties must be literal values.", spanForLine(line));
  }
  const literalProperties: Record<string, Value> | undefined =
    Object.keys(properties).length > 0 && isValueRecord(properties) ? properties : undefined;
  return {
    alias,
    class: call.callee,
    ...(parent ? { parent } : {}),
    ...(literalProperties ? { properties: literalProperties } : {}),
  };
}

function isValueRecord(value: unknown): value is Record<string, Value> {
  return (
    typeof value === "object" &&
    value !== null &&
    !Array.isArray(value) &&
    !isCall(value) &&
    Object.values(value).every(isValue)
  );
}

function isValue(value: unknown): value is Value {
  if (value === null || ["boolean", "number", "string"].includes(typeof value)) {
    return true;
  }
  if (Array.isArray(value)) {
    return value.every(isValue);
  }
  if (typeof value === "object" && value !== null) {
    if ("kind" in value) {
      return value.kind === "name";
    }
    return Object.values(value).every(isValue);
  }
  return false;
}
