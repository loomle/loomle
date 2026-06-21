import type {
  Binding,
  BindingValue,
  Call,
  Condition,
  CreationEntry,
  Detail,
  Edge,
  Expr,
  Find,
  Graph,
  LglObject,
  Node,
  NodeCreation,
  ObjectResult,
  Op,
  Patch,
  Pin,
  PinRef,
  Query,
  Ref,
  SourceSpan,
  Target,
  Value,
} from "./index.js";

interface ParsedLine {
  text: string;
  line: number;
}

interface ParseContext {
  bindings: Binding[];
  assets: Map<string, { path: string; type?: string }>;
  graphs: Map<string, Target>;
}

export function parseLglObject(text: string): ObjectResult {
  try {
    const lines = preprocessLines(text);
    if (lines.length === 0) {
      return errorResult("empty_document", "LGL document is empty.");
    }

    const context = parseLeadingBindings(lines);
    const queryIndex = lines.findIndex((line) => line.text.startsWith("query "));
    const patchIndex = lines.findIndex((line) => line.text.startsWith("patch "));

    if (queryIndex >= 0 && patchIndex >= 0) {
      throw new ParseError("mixed_document_kinds", "A document cannot contain both query and patch statements.", spanForLine(lines[Math.min(queryIndex, patchIndex)]));
    }

    if (queryIndex >= 0) {
      return { object: parseQuery(lines, queryIndex, context), diagnostics: [] };
    }

    if (patchIndex >= 0) {
      return { object: parsePatch(lines, patchIndex, context), diagnostics: [] };
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

function preprocessLines(text: string): ParsedLine[] {
  const result: ParsedLine[] = [];
  const rawLines = text.split(/\r?\n/);

  for (let index = 0; index < rawLines.length; index += 1) {
    const trimmed = rawLines[index].trim();
    if (trimmed === "" || trimmed.startsWith("#")) {
      continue;
    }
    if (trimmed === "---") {
      break;
    }
    result.push({ text: trimmed, line: index + 1 });
  }

  return result;
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

function graphToRef(expr: Expr | undefined, line: ParsedLine): Target["graph"] {
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
  let target: Target | undefined;

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

function parsePatch(lines: ParsedLine[], patchIndex: number, context: ParseContext): Patch {
  const patchLine = lines[patchIndex];
  const match = /^patch\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+(dry)\s+(run))?$/.exec(patchLine.text);
  if (!match) {
    throw new ParseError("invalid_patch", "Expected patch <graph>.", spanForLine(patchLine));
  }

  const patch: Patch = {
    kind: "patch",
    target: resolveGraphTarget(match[1], context, patchLine),
    dryRun: match[2] === "dry" && match[3] === "run",
    bindings: [],
    ops: [],
  };

  for (const line of lines.slice(patchIndex + 1)) {
    const addBinding = tryParseAddBinding(line);
    if (addBinding) {
      patch.bindings.push(normalizePatchBinding(addBinding.binding, line));
      patch.ops.push({ kind: "add", binding: addBinding.name });
      continue;
    }

    const binding = tryParseBinding(line);
    if (binding) {
      patch.bindings.push(normalizePatchBinding(binding, line));
      continue;
    }

    patch.ops.push(...parseOps(line));
  }

  return patch;
}

function tryParseCreationResult(lines: ParsedLine[], context: ParseContext): LglObject | undefined {
  const entries: CreationEntry[] = [];
  let target: Target | undefined = firstGraphTarget(context);

  for (const line of lines) {
    const binding = tryParseBinding(line);
    if (!binding || binding.target.kind !== "local" || !isCall(binding.value)) {
      continue;
    }
    if (binding.value.callee === "node" && typeof binding.value.args.palette === "string") {
      const palette = binding.value.args.palette;
      entries.push({
        name: binding.target.name,
        palette: { kind: "palette", id: palette },
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
  return { kind: "creation_result", target, entries };
}

function normalizePatchBinding(binding: Binding, line: ParsedLine): Binding {
  if (binding.target.kind !== "local" || !isCall(binding.value)) {
    return binding;
  }

  return {
    ...binding,
    value: normalizeCreationCall(binding.value, line) ?? binding.value,
  };
}

function normalizeCreationCall(call: Call, line: ParsedLine): NodeCreation | undefined {
  if (call.callee === "node") {
    const { palette, ...defaults } = call.args;
    if (typeof palette !== "string") {
      return undefined;
    }
    return {
      kind: "palette_node",
      palette,
      ...(Object.keys(defaults).length > 0 ? { defaults } : {}),
    };
  }

  if (isShortcutCall(call)) {
    return {
      kind: "shortcut_node",
      constructor: call,
    };
  }

  if (call.callee === "palette") {
    throw new ParseError("unsupported_palette_binding", "Use node(palette: \"...\") for palette-id node creation.", spanForLine(line));
  }

  return undefined;
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

function graphTargetFromNodeCall(call: Call, context: ParseContext, line: ParsedLine): Target {
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

function tryParseBinding(line: ParsedLine): Binding | undefined {
  const eq = findTopLevel(line.text, "=");
  if (eq < 0 || line.text.startsWith("set ")) {
    return undefined;
  }
  return {
    target: parseBindingTarget(line.text.slice(0, eq).trim(), line),
    value: parseExpr(line.text.slice(eq + 1), line),
  };
}

function tryParseAddBinding(line: ParsedLine): { name: string; binding: Binding } | undefined {
  const match = /^add\s+(.+)$/.exec(line.text);
  if (!match || !match[1].includes("=")) {
    return undefined;
  }
  const synthetic = { text: match[1], line: line.line };
  const binding = tryParseBinding(synthetic);
  if (!binding || binding.target.kind !== "local") {
    throw new ParseError("invalid_add_binding", "add binding sugar requires add name = constructor(...).", spanForLine(line));
  }
  return { name: binding.target.name, binding };
}

function parseBindingTarget(text: string, line: ParsedLine): Binding["target"] {
  const local = /^([A-Za-z_][A-Za-z0-9_]*)$/.exec(text);
  if (local) {
    return { kind: "local", name: local[1] };
  }
  const member = /^([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)$/.exec(text);
  if (member) {
    return { kind: "member", object: member[1], member: member[2] };
  }
  throw new ParseError("invalid_binding_target", "Expected binding target name or object.member.", spanForLine(line));
}

function parseOps(line: ParsedLine): Op[] {
  let match = /^insert\s+(.+)$/.exec(line.text);
  if (match) {
    return [parseInsert(match[1], line)];
  }

  match = /^connect\s+(.+)$/.exec(line.text);
  if (match) {
    return edgesFromChain(parsePinChain(match[1], line)).map((edge) => ({ kind: "connect", edge }));
  }

  match = /^disconnect\s+(.+)$/.exec(line.text);
  if (match) {
    if (match[1].includes("->")) {
      const edges = edgesFromChain(parsePinChain(match[1], line));
      if (edges.length !== 1) {
        throw new ParseError("invalid_disconnect", "Disconnect edge form must describe exactly one edge.", spanForLine(line));
      }
      return [{ kind: "disconnect", edge: edges[0] }];
    }
    return [{ kind: "disconnect", pin: parsePinRef(match[1], line) }];
  }

  match = /^move\s+([A-Za-z_][A-Za-z0-9_]*)\s+(to|by)\s+\((-?\d+(?:\.\d+)?),\s*(-?\d+(?:\.\d+)?)\)$/.exec(line.text);
  if (match) {
    return [
      match[2] === "to"
        ? { kind: "move", node: match[1], mode: "to", at: [Number(match[3]), Number(match[4])] }
        : { kind: "move", node: match[1], mode: "by", delta: [Number(match[3]), Number(match[4])] },
    ];
  }

  match = /^remove\s+([A-Za-z_][A-Za-z0-9_]*)$/.exec(line.text);
  if (match) {
    return [{ kind: "remove", node: match[1] }];
  }

  match = /^add\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+(.+->.+))?$/.exec(line.text);
  if (match) {
    if (!match[2]) {
      return [{ kind: "add", binding: match[1] }];
    }
    const edges = edgesFromChain(parsePinChain(match[2], line));
    if (edges.length !== 1) {
      throw new ParseError("invalid_add_connect", "Add connect form must describe exactly one edge.", spanForLine(line));
    }
    return [{ kind: "add", binding: match[1], connect: edges[0] }];
  }

  match = /^set\s+([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+)$/.exec(line.text);
  if (match) {
    return [
      {
        kind: "set",
        target: { object: match[1], field: match[2] },
        value: parseExpr(match[3], line),
      },
    ];
  }

  match = /^reconstruct\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+preserve\s+links)?$/.exec(line.text);
  if (match) {
    return [{ kind: "reconstruct", node: match[1], preserveLinks: line.text.includes(" preserve links") }];
  }

  if (line.text.includes("->")) {
    return edgesFromChain(parsePinChain(line.text, line)).map((edge) => ({ kind: "connect", edge }));
  }

  throw new ParseError("unsupported_patch_op", "Unsupported patch operation.", spanForLine(line));
}

function parseInsert(text: string, line: ParsedLine): Op {
  const parts = text.split("->").map((part) => part.trim());
  if (parts.length !== 3 || !parts[1].includes("/")) {
    throw new ParseError("invalid_insert", "Insert requires from -> node.Input/Output -> to.", spanForLine(line));
  }
  const [inputText, outputText] = parts[1].split("/").map((part) => part.trim());
  const input = parsePinRef(inputText, line);
  const output = parsePinRef(outputText.includes(".") ? outputText : `${input.node}.${outputText}`, line);
  return {
    kind: "insert",
    node: input.node,
    from: parsePinRef(parts[0], line),
    to: parsePinRef(parts[2], line),
    input,
    output,
  };
}

function parsePinChain(text: string, line: ParsedLine): PinRef[] {
  const parts = text.split("->").map((part) => part.trim());
  if (parts.length < 2) {
    throw new ParseError("invalid_pin_chain", "Pin chains require at least two segments.", spanForLine(line));
  }

  const pins: PinRef[] = [];
  for (const part of parts) {
    if (part.includes("/")) {
      const [input, output] = part.split("/").map((value) => value.trim());
      const inputRef = parsePinRef(input, line);
      pins.push(inputRef, parsePinRef(output.includes(".") ? output : `${inputRef.node}.${output}`, line));
    } else {
      pins.push(parsePinRef(part, line));
    }
  }
  return pins;
}

function edgesFromChain(pins: PinRef[]): Edge[] {
  const edges: Edge[] = [];
  for (let index = 0; index < pins.length - 1; index += 2) {
    edges.push({ from: pins[index], to: pins[index + 1] });
  }
  return edges;
}

function parsePinRef(text: string, line: ParsedLine): PinRef {
  const match = /^([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)$/.exec(text.trim());
  if (!match) {
    throw new ParseError("invalid_pin_ref", "Expected pin reference node.pin.", spanForLine(line));
  }
  return { node: match[1], pin: match[2] };
}

function parseCondition(text: string, line: ParsedLine): Condition {
  return parseOrCondition(text.trim(), line);
}

function parseOrCondition(text: string, line: ParsedLine): Condition {
  const parts = splitByKeyword(text, "or");
  if (parts.length > 1) {
    return { kind: "or", conditions: parts.map((part) => parseAndCondition(part, line)) as [Condition, ...Condition[]] };
  }
  return parseAndCondition(text, line);
}

function parseAndCondition(text: string, line: ParsedLine): Condition {
  const parts = splitByKeyword(text, "and");
  if (parts.length > 1) {
    return { kind: "and", conditions: parts.map((part) => parseNotCondition(part, line)) as [Condition, ...Condition[]] };
  }
  return parseNotCondition(text, line);
}

function parseNotCondition(text: string, line: ParsedLine): Condition {
  const trimmed = trimParens(text.trim());
  if (trimmed.startsWith("not ")) {
    return { kind: "not", condition: parseCondition(trimmed.slice("not ".length), line) };
  }
  return parseComparisonCondition(trimmed, line);
}

function parseComparisonCondition(text: string, line: ParsedLine): Condition {
  const match = /^([A-Za-z_][A-Za-z0-9_.]*)\s*(~=|!=|>=|<=|>|<|=)\s*(.+)$/.exec(text);
  if (!match) {
    throw new ParseError("unsupported_condition", "Unsupported condition.", spanForLine(line));
  }
  const field = { path: match[1].split(".") as [string, ...string[]] };
  const value = parseExpr(match[3], line);
  switch (match[2]) {
    case "=":
      return { kind: "eq", field, value };
    case "!=":
      return { kind: "ne", field, value };
    case "~=":
      return { kind: "contains", field, value };
    case ">":
      return { kind: "compare", op: "gt", field, value };
    case ">=":
      return { kind: "compare", op: "gte", field, value };
    case "<":
      return { kind: "compare", op: "lt", field, value };
    case "<=":
      return { kind: "compare", op: "lte", field, value };
    default:
      return assertNever(match[2] as never);
  }
}

function splitByKeyword(text: string, keyword: "and" | "or"): string[] {
  return splitTopLevel(text, " ")
    .reduce<string[]>((parts, token) => {
      if (token === keyword) {
        parts.push("");
      } else {
        parts[parts.length - 1] = parts[parts.length - 1]
          ? `${parts[parts.length - 1]} ${token}`
          : token;
      }
      return parts;
    }, [""])
    .map((part) => part.trim())
    .filter(Boolean);
}

function trimParens(text: string): string {
  return text.startsWith("(") && text.endsWith(")") ? text.slice(1, -1).trim() : text;
}

function parseDetails(text: string): Detail[] {
  return text.split(",").map((part) => part.trim() as Detail);
}

function parseOrderBy(text: string): Query["orderBy"] {
  return text.split(",").map((part) => {
    const match = /^([A-Za-z_][A-Za-z0-9_.]*)(?:\s+(asc|desc))?$/.exec(part.trim());
    if (!match) {
      return { key: part.trim(), direction: "asc" as const };
    }
    return { key: match[1], direction: (match[2] as "asc" | "desc" | undefined) ?? "asc" };
  });
}

function parsePage(line: ParsedLine): Query["page"] {
  let match = /^page\s+limit\s+(\d+)$/.exec(line.text);
  if (match) {
    return { limit: Number(match[1]) };
  }
  match = /^page\s+after\s+"([^"]+)"$/.exec(line.text);
  if (match) {
    return { after: match[1] };
  }
  throw new ParseError("invalid_page_clause", "Expected page limit <number> or page after \"cursor\".", spanForLine(line));
}

function parseExpr(text: string, line: ParsedLine): Expr {
  const call = tryParseCall(text, line);
  if (call) {
    return call;
  }
  const ref = tryParseRef(text);
  if (ref) {
    return ref;
  }
  return parseValue(text, line);
}

function tryParseCall(text: string, line: ParsedLine): Call | undefined {
  const match = /^([A-Za-z_][A-Za-z0-9_]*)\((.*)\)$/.exec(text.trim());
  if (!match) {
    return undefined;
  }
  return {
    kind: "call",
    callee: match[1],
    args: parseCallArgs(match[2], line),
  };
}

function parseCallArgs(text: string, line: ParsedLine): Record<string, Expr> {
  const trimmed = text.trim();
  if (trimmed === "") {
    return {};
  }
  const result: Record<string, Expr> = {};
  for (const part of splitTopLevel(trimmed, ",")) {
    const colon = findTopLevel(part, ":");
    if (colon < 0) {
      throw new ParseError("invalid_call_args", "Constructor arguments must use name: value.", spanForLine(line));
    }
    result[part.slice(0, colon).trim()] = parseExpr(part.slice(colon + 1), line);
  }
  return result;
}

function tryParseRef(text: string): Ref | undefined {
  const trimmed = text.trim();
  const id = /^@([A-Za-z0-9_-]+)$/.exec(trimmed);
  if (id) {
    return { kind: "id", id: id[1] };
  }
  const member = /^([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)$/.exec(trimmed);
  if (member) {
    return { kind: "member", object: member[1], member: member[2] };
  }
  const local = /^([A-Za-z_][A-Za-z0-9_]*)$/.exec(trimmed);
  if (local && !isLiteralWord(local[1])) {
    return { kind: "local", name: local[1] };
  }
  return undefined;
}

function parseValue(text: string, line: ParsedLine): Value {
  const trimmed = text.trim();
  if (trimmed === "null") {
    return null;
  }
  if (trimmed === "true") {
    return true;
  }
  if (trimmed === "false") {
    return false;
  }
  if (/^-?\d+(?:\.\d+)?$/.test(trimmed)) {
    return Number(trimmed);
  }
  if (trimmed.startsWith('"') && trimmed.endsWith('"')) {
    return JSON.parse(trimmed) as string;
  }
  if (trimmed.startsWith("[") && trimmed.endsWith("]")) {
    return parseArrayLiteral(trimmed, line);
  }
  if (trimmed.startsWith("{") && trimmed.endsWith("}")) {
    return parseObjectLiteral(trimmed, line);
  }
  if (/^[A-Za-z_][A-Za-z0-9_]*$/.test(trimmed)) {
    return { kind: "name", name: trimmed };
  }

  throw new ParseError("unsupported_value", `Unsupported value: ${trimmed}`, spanForLine(line));
}

function parseObjectLiteral(text: string, line: ParsedLine): Record<string, Value> {
  const inner = unwrap(text, "{", "}", line);
  if (inner.trim() === "") {
    return {};
  }

  const result: Record<string, Value> = {};
  for (const part of splitTopLevel(inner, ",")) {
    const colon = findTopLevel(part, ":");
    if (colon < 0) {
      throw new ParseError("invalid_object", "Object literal entries must use key: value.", spanForLine(line));
    }
    result[part.slice(0, colon).trim()] = parseValue(part.slice(colon + 1), line);
  }
  return result;
}

function parseArrayLiteral(text: string, line: ParsedLine): Value[] {
  const inner = unwrap(text, "[", "]", line);
  if (inner.trim() === "") {
    return [];
  }
  return splitTopLevel(inner, ",").map((part) => parseValue(part, line));
}

function parsePoint(value: Value[], line: ParsedLine): [number, number] {
  if (value.length !== 2 || typeof value[0] !== "number" || typeof value[1] !== "number") {
    throw new ParseError("invalid_point", "Expected a two-number point.", spanForLine(line));
  }
  return [value[0], value[1]];
}

function resolveGraphTarget(name: string, context: ParseContext, line: ParsedLine): Target {
  const target = context.graphs.get(name);
  if (!target) {
    throw new ParseError("unknown_graph_binding", `Unknown graph binding ${name}.`, spanForLine(line));
  }
  return target;
}

function firstGraphTarget(context: ParseContext): Target | undefined {
  return context.graphs.values().next().value as Target | undefined;
}

function unwrap(text: string, open: string, close: string, line: ParsedLine): string {
  const trimmed = text.trim();
  if (!trimmed.startsWith(open) || !trimmed.endsWith(close)) {
    throw new ParseError("invalid_wrapped_value", `Expected ${open}...${close}.`, spanForLine(line));
  }
  return trimmed.slice(open.length, -close.length);
}

function splitTopLevel(text: string, separator: string): string[] {
  const parts: string[] = [];
  let depth = 0;
  let inString = false;
  let start = 0;

  for (let index = 0; index < text.length; index += 1) {
    const char = text[index];
    const prev = text[index - 1];
    if (char === '"' && prev !== "\\") {
      inString = !inString;
      continue;
    }
    if (inString) {
      continue;
    }
    if (char === "(" || char === "[" || char === "{") {
      depth += 1;
    } else if (char === ")" || char === "]" || char === "}") {
      depth -= 1;
    } else if (depth === 0 && text.slice(index, index + separator.length) === separator) {
      parts.push(text.slice(start, index).trim());
      start = index + separator.length;
      index += separator.length - 1;
    }
  }

  parts.push(text.slice(start).trim());
  return parts.filter((part) => part.length > 0);
}

function findTopLevel(text: string, token: string): number {
  let depth = 0;
  let inString = false;

  for (let index = 0; index < text.length; index += 1) {
    const char = text[index];
    const prev = text[index - 1];
    if (char === '"' && prev !== "\\") {
      inString = !inString;
      continue;
    }
    if (inString) {
      continue;
    }
    if (char === "(" || char === "[" || char === "{") {
      depth += 1;
    } else if (char === ")" || char === "]" || char === "}") {
      depth -= 1;
    } else if (depth === 0 && text.slice(index, index + token.length) === token) {
      return index;
    }
  }

  return -1;
}

function isCall(value: BindingValue | Expr | undefined): value is Call {
  return typeof value === "object" && value !== null && !Array.isArray(value) && value.kind === "call";
}

function isName(value: Expr | undefined): value is { kind: "name"; name: string } {
  return typeof value === "object" && value !== null && !Array.isArray(value) && value.kind === "name";
}

function isLocalRef(value: Expr | undefined): value is { kind: "local"; name: string } {
  return typeof value === "object" && value !== null && !Array.isArray(value) && value.kind === "local";
}

function symbolName(value: Expr | undefined): string | undefined {
  if (isName(value) || isLocalRef(value)) {
    return value.name;
  }
  if (typeof value === "string") {
    return value;
  }
  return undefined;
}

function isLiteralWord(value: string): boolean {
  return value === "true" || value === "false" || value === "null";
}

function spanForLine(line: ParsedLine): SourceSpan {
  return { line: line.line, column: 1, length: line.text.length };
}

class ParseError extends Error {
  constructor(
    readonly code: string,
    message: string,
    readonly span: SourceSpan,
  ) {
    super(message);
  }
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
