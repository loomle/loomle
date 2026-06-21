import type {
  Binding,
  Call,
  CreationEntry,
  Edge,
  Expr,
  Find,
  Graph,
  GraphTarget,
  LglObject,
  Node,
  ObjectResult,
  Pin,
  PinRef,
  Query,
} from "../index.js";
import { parseAssetQuery, tryParseAssetResult } from "../asset/parser.js";
import { parseBlueprintBindings, parseBlueprintQuery, tryParseBlueprintResult } from "../blueprint/parser.js";
import { parseBlueprintPatch } from "../blueprint/patch-parser.js";
import { parseWidgetBindings, parseWidgetQuery, tryParseWidgetCreationResult, tryParseWidgetResult } from "../widget/parser.js";
import { parseWidgetPatch } from "../widget/patch-parser.js";
import { tryParseBinding } from "../core/binding.js";
import { parseCondition, parseDetails, parseOrderBy, parsePage } from "../core/condition.js";
import { isCall, isLocalRef, parsePoint, symbolName } from "../core/expr.js";
import {
  ParseError,
  type ParsedLine,
  preprocessLines,
  spanForLine,
} from "../core/text.js";
import { parseGraphPatch } from "./patch-parser.js";
import { edgesFromChain, parsePinChain, parsePinRef } from "./pins.js";

interface ParseContext {
  bindings: Binding[];
  assets: Map<string, { path: string; type?: string }>;
  graphs: Map<string, GraphTarget>;
}

export function parseLglObject(text: string): ObjectResult {
  try {
    const lines = preprocessLines(text);
    if (lines.length === 0) {
      return errorResult("empty_document", "LGL document is empty.");
    }

    const context = parseLeadingBindings(lines);
    const blueprintBindings = parseBlueprintBindings(lines);
    const widgetBindings = parseWidgetBindings(lines);
    const queryIndex = lines.findIndex((line) => line.text.startsWith("query "));
    const patchIndex = lines.findIndex((line) => line.text.startsWith("patch "));

    if (queryIndex >= 0 && patchIndex >= 0) {
      throw new ParseError("mixed_document_kinds", "A document cannot contain both query and patch statements.", spanForLine(lines[Math.min(queryIndex, patchIndex)]));
    }

    if (queryIndex >= 0 && lines[queryIndex].text === "query asset") {
      return { object: parseAssetQuery(lines, queryIndex), diagnostics: [] };
    }

    if (queryIndex >= 0 && isBlueprintQuery(lines[queryIndex], blueprintBindings)) {
      return { object: parseBlueprintQuery(lines, queryIndex, blueprintBindings), diagnostics: [] };
    }

    if (queryIndex >= 0 && isWidgetQuery(lines[queryIndex], widgetBindings)) {
      return { object: parseWidgetQuery(lines, queryIndex, widgetBindings), diagnostics: [] };
    }

    if (queryIndex >= 0) {
      return { object: parseQuery(lines, queryIndex, context), diagnostics: [] };
    }

    if (patchIndex >= 0) {
      if (isBlueprintPatch(lines[patchIndex], blueprintBindings)) {
        return { object: parseBlueprintPatch(lines, patchIndex, blueprintBindings), diagnostics: [] };
      }
      if (isWidgetPatch(lines[patchIndex], widgetBindings)) {
        return { object: parseWidgetPatch(lines, patchIndex, widgetBindings), diagnostics: [] };
      }
      return {
        object: parseGraphPatch(lines, patchIndex, (name, line) => resolveGraphTarget(name, context, line)),
        diagnostics: [],
      };
    }

    const assetResult = tryParseAssetResult(lines);
    if (assetResult) {
      return { object: assetResult, diagnostics: [] };
    }

    const blueprintResult = tryParseBlueprintResult(lines);
    if (blueprintResult) {
      return { object: blueprintResult, diagnostics: [] };
    }

    const widgetResult = tryParseWidgetResult(lines);
    if (widgetResult) {
      return { object: widgetResult, diagnostics: [] };
    }

    const widgetCreationResult = tryParseWidgetCreationResult(lines, widgetBindings);
    if (widgetCreationResult) {
      return { object: widgetCreationResult, diagnostics: [] };
    }

    const creationResult = tryParseCreationResult(lines, context);
    if (creationResult) {
      return { object: creationResult, diagnostics: [] };
    }

    return { object: parseGraph(lines, context), diagnostics: [] };
  } catch (error) {
    if (error instanceof ParseError) {
      return {
        diagnostics: [
          {
            severity: "error",
            code: error.code,
            message: error.message,
            span: error.span,
          },
        ],
      };
    }

    throw error;
  }
}

function isBlueprintQuery(line: ParsedLine, bindings: Map<string, unknown>): boolean {
  const match = /^query\s+([A-Za-z_][A-Za-z0-9_]*)$/.exec(line.text);
  return !!match && bindings.has(match[1]);
}

function isBlueprintPatch(line: ParsedLine, bindings: Map<string, unknown>): boolean {
  const match = /^patch\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+dry\s+run)?$/.exec(line.text);
  return !!match && bindings.has(match[1]);
}

function isWidgetQuery(line: ParsedLine, bindings: Map<string, unknown>): boolean {
  const match = /^query\s+([A-Za-z_][A-Za-z0-9_]*)$/.exec(line.text);
  return !!match && bindings.has(match[1]);
}

function isWidgetPatch(line: ParsedLine, bindings: Map<string, unknown>): boolean {
  const match = /^patch\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+dry\s+run)?$/.exec(line.text);
  return !!match && bindings.has(match[1]);
}

function parseLeadingBindings(lines: ParsedLine[]): ParseContext {
  const context: ParseContext = {
    bindings: [],
    assets: new Map(),
    graphs: new Map(),
  };

  for (const line of lines) {
    if (line.text.startsWith("query ") || line.text.startsWith("patch ")) {
      break;
    }
    const binding = tryParseBinding(line);
    if (!binding) {
      continue;
    }
    context.bindings.push(binding);
    registerBinding(context, binding, line);
  }

  return context;
}

function registerBinding(context: ParseContext, binding: Binding, line: ParsedLine): void {
  if (binding.target.kind !== "local" || !isCall(binding.value)) {
    return;
  }

  if (binding.value.callee === "asset") {
    const path = binding.value.args.path;
    if (typeof path !== "string") {
      throw new ParseError("invalid_asset_binding", "asset(...) requires path: string.", spanForLine(line));
    }
    const type = symbolName(binding.value.args.type);
    context.assets.set(binding.target.name, {
      path,
      ...(type ? { type } : {}),
    });
    return;
  }

  if (binding.value.callee === "graph") {
    const domain = binding.value.args.domain;
    const asset = binding.value.args.asset;
    const graph = binding.value.args.graph;
    const domainName = symbolName(domain);
    if (!domainName) {
      throw new ParseError("invalid_graph_binding", "graph(...) requires domain: symbol.", spanForLine(line));
    }
    if (!isLocalRef(asset)) {
      throw new ParseError("invalid_graph_binding", "graph(...) requires asset: assetBinding.", spanForLine(line));
    }
    const assetBinding = context.assets.get(asset.name);
    if (!assetBinding) {
      throw new ParseError("unknown_asset_binding", `Unknown asset binding ${asset.name}.`, spanForLine(line));
    }
    context.graphs.set(binding.target.name, {
      domain: domainName,
      asset: assetBinding.path,
      graph: graphToRef(graph, line),
    });
  }
}

function graphToRef(expr: Expr | undefined, line: ParsedLine): GraphTarget["graph"] {
  const name = symbolName(expr);
  if (name) {
    return { kind: "name", name };
  }
  if (isCall(expr) && expr.callee === "id" && typeof expr.args.id === "string") {
    return { kind: "id", id: expr.args.id };
  }
  throw new ParseError("invalid_graph_ref", "graph(...) requires graph: Name or id(id: string).", spanForLine(line));
}

function parseGraph(lines: ParsedLine[], context: ParseContext): Graph {
  const nodes: Node[] = [];
  const pins: Pin[] = [];
  const edges: Edge[] = [];
  let target: GraphTarget | undefined;

  for (const line of lines) {
    const binding = tryParseBinding(line);
    if (binding) {
      registerBinding(context, binding, line);
      if (binding.target.kind === "local" && isCall(binding.value) && binding.value.callee === "node") {
        const node = nodeFromBinding(binding, line);
        target = target ?? graphTargetFromNodeCall(binding.value, context, line);
        nodes.push(node);
        continue;
      }
      if (binding.target.kind === "member" && isCall(binding.value) && binding.value.callee === "pin") {
        pins.push(pinFromBinding(binding, line));
        continue;
      }
      continue;
    }

    if (line.text.includes("->")) {
      edges.push(...edgesFromChain(parsePinChain(line.text, line)));
      continue;
    }

    throw new ParseError("unsupported_object_statement", "Unsupported object text statement.", spanForLine(line));
  }

  target = target ?? firstGraphTarget(context);
  if (!target) {
    throw new ParseError("missing_graph_binding", "Graph object text requires a graph binding or node(graph: ...).", spanForLine(lines[0]));
  }

  return {
    kind: "graph",
    target,
    nodes,
    edges,
    ...(pins.length > 0 ? { pins } : {}),
  };
}

function parseQuery(lines: ParsedLine[], queryIndex: number, context: ParseContext): Query {
  const queryLine = lines[queryIndex];
  const match = /^query\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+dry\s+run)?$/.exec(queryLine.text);
  if (!match) {
    throw new ParseError("invalid_query", "Expected query <graph>.", spanForLine(queryLine));
  }

  const query: Query = {
    kind: "query",
    target: resolveGraphTarget(match[1], context, queryLine),
  };

  for (const line of lines.slice(queryIndex + 1)) {
    if (line.text.startsWith("find ")) {
      query.find = parseFind(line);
    } else if (line.text.startsWith("where ")) {
      query.where = parseCondition(line.text.slice("where ".length), line);
    } else if (line.text.startsWith("with ")) {
      query.with = parseDetails(line.text.slice("with ".length));
    } else if (line.text.startsWith("order by ")) {
      query.orderBy = parseOrderBy(line.text.slice("order by ".length));
    } else if (line.text.startsWith("page ")) {
      query.page = { ...(query.page ?? {}), ...parsePage(line) };
    } else {
      throw new ParseError("unsupported_query_clause", "Unsupported query clause.", spanForLine(line));
    }
  }

  return query;
}

function tryParseCreationResult(lines: ParsedLine[], context: ParseContext): LglObject | undefined {
  const entries: CreationEntry[] = [];
  const pins: Pin[] = [];
  let target: GraphTarget | undefined = firstGraphTarget(context);

  for (const line of lines) {
    const binding = tryParseBinding(line);
    if (!binding) {
      continue;
    }
    if (binding.target.kind === "member" && isCall(binding.value) && binding.value.callee === "pin") {
      pins.push(pinFromBinding(binding, line));
      continue;
    }
    if (binding.target.kind !== "local" || !isCall(binding.value)) {
      continue;
    }
    if (binding.value.callee === "node" && typeof binding.value.args.palette === "string") {
      const { palette, ...defaults } = binding.value.args;
      entries.push({
        name: binding.target.name,
        palette: { kind: "palette", id: palette },
        ...(Object.keys(defaults).length > 0 ? { defaults } : {}),
      });
      continue;
    }
    if (isShortcutCall(binding.value)) {
      entries.push({
        name: binding.target.name,
        constructor: binding.value,
      });
    }
  }

  if (entries.length === 0) {
    return undefined;
  }
  target = target ?? firstGraphTarget(context);
  if (!target) {
    throw new ParseError("missing_graph_binding", "Creation result text requires a graph binding.", spanForLine(lines[0]));
  }
  for (const pin of pins) {
    const entry = entries.find((candidate) => candidate.name === pin.node);
    if (entry && !("class" in entry)) {
      entry.pins = [...(entry.pins ?? []), pin];
    }
  }
  return { kind: "creation_result", target, entries };
}

function isShortcutCall(call: Call): boolean {
  return !["asset", "graph", "node", "pin", "palette"].includes(call.callee);
}

function nodeFromBinding(binding: Binding, line: ParsedLine): Node {
  if (binding.target.kind !== "local" || !isCall(binding.value) || binding.value.callee !== "node") {
    throw new ParseError("invalid_node", "Expected name = node(...).", spanForLine(line));
  }

  const { graph: _graph, type, id, at, size, ...fields } = binding.value.args;
  const typeName = symbolName(type);
  if (!typeName) {
    throw new ParseError("invalid_node", "node(...) requires type: symbol.", spanForLine(line));
  }

  return {
    alias: binding.target.name,
    type: typeName,
    ...(typeof id === "string" ? { id } : {}),
    fields,
    ...(Array.isArray(at) ? { at: parsePoint(at, line) } : {}),
    ...(Array.isArray(size) ? { size: parsePoint(size, line) } : {}),
  };
}

function graphTargetFromNodeCall(call: Call, context: ParseContext, line: ParsedLine): GraphTarget {
  const graph = call.args.graph;
  if (!isLocalRef(graph)) {
    throw new ParseError("invalid_node", "node(...) requires graph: graphBinding.", spanForLine(line));
  }
  return resolveGraphTarget(graph.name, context, line);
}

function pinFromBinding(binding: Binding, line: ParsedLine): Pin {
  if (binding.target.kind !== "member" || !isCall(binding.value) || binding.value.callee !== "pin") {
    throw new ParseError("invalid_pin", "Expected node.pin = pin(...).", spanForLine(line));
  }
  const type = binding.value.args.type;
  const direction = binding.value.args.direction;
  const typeName = symbolName(type);
  const directionName = symbolName(direction);
  if (!typeName || (directionName !== "in" && directionName !== "out")) {
    throw new ParseError("invalid_pin", "pin(...) requires type: symbol and direction: in|out.", spanForLine(line));
  }

  const value = binding.value.args.value;
  const anchor = binding.value.args.anchor;
  return {
    node: binding.target.object,
    name: binding.target.member,
    type: typeName,
    direction: directionName,
    ...(value !== undefined ? { value } : {}),
    ...(Array.isArray(anchor) ? { anchor: parsePoint(anchor, line) } : {}),
  };
}

function parseFind(line: ParsedLine): Find {
  let match = /^find nodes(?:\s+"([^"]+)")?$/.exec(line.text);
  if (match) {
    return { kind: "nodes", ...(match[1] ? { text: match[1] } : {}) };
  }

  match = /^find path\s+(from|to)\s+(.+)$/.exec(line.text);
  if (match) {
    return { kind: "path", direction: match[1] as "from" | "to", pin: parsePinRef(match[2], line) };
  }

  match = /^find palette entry(?:\s+"([^"]+)")?(?:\s+(from|to)\s+(.+))?$/.exec(line.text);
  if (match) {
    return {
      kind: "palette_entry",
      ...(match[1] ? { text: match[1] } : {}),
      ...(match[2] && match[3]
        ? { pinContext: { direction: match[2] as "from" | "to", pin: parsePinRef(match[3], line) } }
        : {}),
    };
  }

  throw new ParseError("unsupported_query", "Unsupported query find form.", spanForLine(line));
}

function resolveGraphTarget(name: string, context: ParseContext, line: ParsedLine): GraphTarget {
  const target = context.graphs.get(name);
  if (!target) {
    throw new ParseError("unknown_graph_binding", `Unknown graph binding ${name}.`, spanForLine(line));
  }
  return target;
}

function firstGraphTarget(context: ParseContext): GraphTarget | undefined {
  return context.graphs.values().next().value as GraphTarget | undefined;
}

function errorResult(code: string, message: string): ObjectResult {
  return {
    diagnostics: [
      {
        severity: "error",
        code,
        message,
      },
    ],
  };
}

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}
