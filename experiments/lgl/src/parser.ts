import type {
  Binding,
  Call,
  Condition,
  Detail,
  Edge,
  Expr,
  Find,
  Graph,
  LglObject,
  Node,
  ObjectResult,
  Op,
  Palette,
  PaletteBinding,
  Patch,
  Pin,
  PinChain,
  PinChainSegment,
  PinRef,
  Query,
  SourceSpan,
  Target,
  Value,
} from "./index.js";

type DocumentKind = LglObject["kind"];

interface ParsedHeader {
  kind: DocumentKind;
  target: Target;
  dryRun: boolean;
}

interface ParsedLine {
  text: string;
  line: number;
}

export function parseLglObject(text: string): ObjectResult {
  try {
    const lines = preprocessLines(text);
    if (lines.length === 0) {
      return errorResult("empty_document", "LGL document is empty.");
    }

    const header = parseHeader(lines[0]);
    const body = lines.slice(1);

    switch (header.kind) {
      case "graph":
        return { object: parseGraph(header, body), diagnostics: [] };
      case "query":
        return { object: parseQuery(header, body), diagnostics: [] };
      case "patch":
        return { object: parsePatch(header, body), diagnostics: [] };
      case "palette":
        return { object: parsePalette(header, body), diagnostics: [] };
      default:
        return assertNever(header.kind);
    }
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

function parseHeader(line: ParsedLine): ParsedHeader {
  const match = /^(graph|query|patch|palette)\s+([A-Za-z_][A-Za-z0-9_]*)\("([^"]+)"\/(.+)\)(?:\s+(dry)\s+(run))?$/.exec(
    line.text,
  );
  if (!match) {
    throw new ParseError(
      "invalid_header",
      "Expected a document header such as query blueprint(\"/Game/BP\"/EventGraph).",
      spanForLine(line),
    );
  }

  const graphText = match[4].trim();
  const graphIdMatch = /^id\("([^"]+)"\)$/.exec(graphText);

  return {
    kind: match[1] as DocumentKind,
    target: {
      domain: match[2],
      asset: match[3],
      graph: graphIdMatch
        ? { kind: "id", id: graphIdMatch[1] }
        : { kind: "name", name: graphText },
    },
    dryRun: match[5] === "dry" && match[6] === "run",
  };
}

function parseGraph(header: ParsedHeader, body: ParsedLine[]): Graph {
  const nodes: Node[] = [];
  const pins: Pin[] = [];
  const edges: Edge[] = [];

  for (const line of body) {
    if (line.text.includes("->")) {
      edges.push(...edgesFromChain(parsePinChain(line.text, line)));
      continue;
    }

    if (isPinLine(line.text)) {
      pins.push(parsePin(line));
      continue;
    }

    nodes.push(parseNode(line));
  }

  const graph: Graph = {
    kind: "graph",
    target: header.target,
    nodes,
    edges,
  };

  if (pins.length > 0) {
    graph.pins = pins;
  }

  return graph;
}

function parseQuery(header: ParsedHeader, body: ParsedLine[]): Query {
  if (body.length === 0) {
    return { kind: "query", target: header.target };
  }
  if (body.length > 1) {
    throw new ParseError(
      "unsupported_query_body",
      "The first parser version accepts one query statement per document.",
      spanForLine(body[1]),
    );
  }

  return {
    kind: "query",
    target: header.target,
    find: parseFind(body[0]),
  };
}

function parsePatch(header: ParsedHeader, body: ParsedLine[]): Patch {
  const bindings: Binding[] = [];
  const ops: Op[] = [];

  for (const line of body) {
    const binding = tryParseBinding(line);
    if (binding) {
      bindings.push(binding);
      continue;
    }

    ops.push(parseOp(line));
  }

  return {
    kind: "patch",
    target: header.target,
    dryRun: header.dryRun,
    bindings,
    ops,
  };
}

function parsePalette(header: ParsedHeader, body: ParsedLine[]): Palette {
  const entries: PaletteBinding[] = body.map((line) => {
    const binding = tryParseBinding(line);
    if (!binding || !isCall(binding.value) || binding.value.callee !== "palette") {
      throw new ParseError(
        "invalid_palette_binding",
        "Palette documents must contain Name = palette({...}) bindings.",
        spanForLine(line),
      );
    }

    const id = binding.value.args.id;
    if (typeof id !== "string") {
      throw new ParseError(
        "invalid_palette_id",
        "Palette binding id must be a string.",
        spanForLine(line),
      );
    }

    const { id: _id, ...meta } = binding.value.args;
    return {
      name: binding.name,
      entry: { kind: "palette", id },
      ...(Object.keys(meta).length > 0 ? { meta } : {}),
    };
  });

  return {
    kind: "palette",
    target: header.target,
    entries,
  };
}

function parseNode(line: ParsedLine): Node {
  const [semanticText, layoutText] = splitTrailingObject(line.text);
  const match = /^([A-Za-z_@][A-Za-z0-9_@]*)(?:@([A-Za-z0-9_-]+))?:\s*([A-Za-z_][A-Za-z0-9_]*)\((.*)\)$/.exec(
    semanticText,
  );
  if (!match) {
    throw new ParseError("invalid_node", "Expected node line alias@id: Type({...}).", spanForLine(line));
  }

  const node: Node = {
    alias: match[1],
    ...(match[2] ? { id: match[2] } : {}),
    type: match[3],
    fields: parseCallArgs(match[4], line),
  };

  if (layoutText) {
    const layout = parseObjectLiteral(layoutText, line);
    node.layout = {};
    if (Array.isArray(layout.at)) {
      node.layout.at = parsePoint(layout.at, line);
    }
    if (Array.isArray(layout.size)) {
      node.layout.size = parsePoint(layout.size, line);
    }
  }

  return node;
}

function parsePin(line: ParsedLine): Pin {
  const [semanticText, metaText] = splitTrailingObject(line.text);
  const match = /^([A-Za-z_@][A-Za-z0-9_@]*)\.([A-Za-z_][A-Za-z0-9_]*):\s*([^ ]+)\s+(in|out)$/.exec(
    semanticText,
  );
  if (!match) {
    throw new ParseError("invalid_pin", "Expected pin line node.pin: type in|out.", spanForLine(line));
  }

  const pin: Pin = {
    node: match[1],
    name: match[2],
    type: match[3],
    direction: match[4] as "in" | "out",
  };

  if (metaText) {
    const pinMeta = parsePinMeta(metaText, line);
    if ("value" in pinMeta) {
      pin.value = pinMeta.value;
    }
    if (pinMeta.anchor) {
      pin.layout = { anchor: pinMeta.anchor };
    }
  }

  return pin;
}

function parseFind(line: ParsedLine): Find {
  let match = /^find node\s+([A-Za-z_@][A-Za-z0-9_@]*)(?:\s+with\s+(.+))?$/.exec(line.text);
  if (match) {
    return {
      kind: "node",
      node: match[1],
      ...(match[2] ? { with: parseDetails(match[2]) } : {}),
    };
  }

  match = /^find nodes(?:\s+where\s+(.+?))?(?:\s+with\s+(.+))?$/.exec(line.text);
  if (match) {
    return {
      kind: "nodes",
      ...(match[1] ? { where: parseCondition(match[1], line) } : {}),
      ...(match[2] ? { with: parseDetails(match[2]) } : {}),
    };
  }

  match = /^find path from\s+(.+)$/.exec(line.text);
  if (match) {
    return { kind: "path", from: parsePinRef(match[1], line) };
  }

  match = /^find surrounding around\s+([A-Za-z_@][A-Za-z0-9_@]*)\s+depth\s+(\d+)$/.exec(line.text);
  if (match) {
    return { kind: "surrounding", around: match[1], depth: Number(match[2]) };
  }

  match = /^find palette entry(?:\s+"([^"]+)")?(?:\s+where\s+(.+))?$/.exec(line.text);
  if (match) {
    if (!match[1] && !match[2]) {
      throw new ParseError(
        "invalid_palette_query",
        "Palette entry queries require text, a where condition, or both.",
        spanForLine(line),
      );
    }
    return {
      kind: "palette_entry",
      ...(match[1] ? { text: match[1] } : {}),
      ...(match[2] ? { where: parseCondition(match[2], line) } : {}),
    };
  }

  throw new ParseError("unsupported_query", "Unsupported query statement.", spanForLine(line));
}

function tryParseBinding(line: ParsedLine): Binding | undefined {
  const match = /^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+)$/.exec(line.text);
  if (!match) {
    return undefined;
  }

  return {
    name: match[1],
    value: parseExpr(match[2], line),
  };
}

function parseOp(line: ParsedLine): Op {
  let match = /^insert\s+(.+)$/.exec(line.text);
  if (match) {
    const chain = parsePinChain(match[1], line);
    const through = chain.segments.find((segment) => segment.kind === "through");
    if (!through || through.kind !== "through") {
      throw new ParseError("invalid_insert", "Insert requires a through segment for the inserted node.", spanForLine(line));
    }
    return {
      kind: "insert",
      node: through.input.node,
      chain,
    };
  }

  match = /^connect\s+(.+)$/.exec(line.text);
  if (match) {
    return { kind: "connect", chain: parsePinChain(match[1], line) };
  }

  match = /^disconnect\s+(.+)$/.exec(line.text);
  if (match) {
    if (match[1].includes("->")) {
      const edges = edgesFromChain(parsePinChain(match[1], line));
      if (edges.length !== 1) {
        throw new ParseError("invalid_disconnect", "Disconnect edge form must describe exactly one edge.", spanForLine(line));
      }
      return { kind: "disconnect", edge: edges[0] };
    }
    return { kind: "disconnect", pin: parsePinRef(match[1], line) };
  }

  match = /^move\s+([A-Za-z_@][A-Za-z0-9_@]*)\s+(to|by)\s+\((-?\d+(?:\.\d+)?),\s*(-?\d+(?:\.\d+)?)\)$/.exec(line.text);
  if (match) {
    if (match[2] === "to") {
      return { kind: "move", node: match[1], mode: "to", at: [Number(match[3]), Number(match[4])] };
    }
    return { kind: "move", node: match[1], mode: "by", delta: [Number(match[3]), Number(match[4])] };
  }

  match = /^remove\s+([A-Za-z_@][A-Za-z0-9_@]*)$/.exec(line.text);
  if (match) {
    return { kind: "remove", node: match[1] };
  }

  match = /^add\s+([A-Za-z_@][A-Za-z0-9_@]*)$/.exec(line.text);
  if (match) {
    return { kind: "add", node: match[1] };
  }

  match = /^set\s+([A-Za-z_@][A-Za-z0-9_@]*)\.([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+)$/.exec(line.text);
  if (match) {
    return {
      kind: "set",
      target: { node: match[1], field: match[2] },
      value: parseExpr(match[3], line),
    };
  }

  match = /^reconstruct\s+([A-Za-z_@][A-Za-z0-9_@]*)\s+preserve\s+links$/.exec(line.text);
  if (match) {
    return { kind: "reconstruct", node: match[1], preserveLinks: true };
  }

  if (line.text.includes("->")) {
    return { kind: "connect", chain: parsePinChain(line.text, line) };
  }

  throw new ParseError("unsupported_patch_op", "Unsupported patch operation.", spanForLine(line));
}

function parsePinChain(text: string, line: ParsedLine): PinChain {
  const parts = text.split("->").map((part) => part.trim());
  if (parts.length < 2) {
    throw new ParseError("invalid_pin_chain", "Pin chains require at least two segments.", spanForLine(line));
  }

  const segments = parts.map((part, index) => {
    if (index > 0 && index < parts.length - 1 && part.includes("/")) {
      const [input, output] = part.split("/").map((value) => value.trim());
      return {
        kind: "through",
        input: parsePinRef(input, line),
        output: parsePinRef(output.includes(".") ? output : `${input.split(".")[0]}.${output}`, line),
      };
    }
    return { kind: "pin", pin: parsePinRef(part, line) };
  }) as [PinChainSegment, PinChainSegment, ...PinChainSegment[]];

  return { segments };
}

function edgesFromChain(chain: PinChain): Edge[] {
  const pins: PinRef[] = [];
  for (const segment of chain.segments) {
    if (segment.kind === "pin") {
      pins.push(segment.pin);
    } else {
      pins.push(segment.input, segment.output);
    }
  }

  const edges: Edge[] = [];
  for (let index = 0; index < pins.length - 1; index += 2) {
    edges.push({ from: pins[index], to: pins[index + 1] });
  }
  return edges;
}

function parsePinRef(text: string, line: ParsedLine): PinRef {
  const match = /^([A-Za-z_@][A-Za-z0-9_@]*)\.([A-Za-z_][A-Za-z0-9_]*)$/.exec(text.trim());
  if (!match) {
    throw new ParseError("invalid_pin_ref", "Expected pin reference node.pin.", spanForLine(line));
  }
  return { node: match[1], pin: match[2] };
}

function parseCondition(text: string, line: ParsedLine): Condition {
  const containsMatch = /^([A-Za-z_][A-Za-z0-9_. ]*)\s+contains\s+(.+)$/.exec(text);
  if (containsMatch) {
    return {
      kind: "contains",
      field: containsMatch[1].trim(),
      value: parseValue(containsMatch[2], line),
    };
  }

  const eqMatch = /^([A-Za-z_][A-Za-z0-9_. ]*)\s*=\s*(.+)$/.exec(text);
  if (eqMatch) {
    return {
      kind: "eq",
      field: eqMatch[1].trim(),
      value: parseValue(eqMatch[2], line),
    };
  }

  throw new ParseError("unsupported_condition", "Unsupported condition.", spanForLine(line));
}

function parseDetails(text: string): Detail[] {
  return text.split(",").map((part) => part.trim() as Detail);
}

function parseExpr(text: string, line: ParsedLine): Expr {
  const call = tryParseCall(text, line);
  return call ?? parseValue(text, line);
}

function parseCallArgs(text: string, line: ParsedLine): Record<string, Value> {
  const trimmed = text.trim();
  if (trimmed === "") {
    return {};
  }
  const value = parseValue(trimmed, line);
  if (!isPlainObject(value)) {
    throw new ParseError("invalid_call_args", "Call arguments must be an object literal.", spanForLine(line));
  }
  return value;
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
    const key = part.slice(0, colon).trim();
    result[key] = parseValue(part.slice(colon + 1), line);
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

function parsePinMeta(text: string, line: ParsedLine): { value?: Value; anchor?: [number, number] } {
  const inner = unwrap(text, "{", "}", line);
  const result: { value?: Value; anchor?: [number, number] } = {};
  if (inner.trim() === "") {
    return result;
  }

  for (const part of splitTopLevel(inner, ",")) {
    const colon = findTopLevel(part, ":");
    if (colon >= 0) {
      const key = part.slice(0, colon).trim();
      if (key === "anchor") {
        const value = parseValue(part.slice(colon + 1), line);
        if (!Array.isArray(value)) {
          throw new ParseError("invalid_anchor", "Pin anchor must be a point array.", spanForLine(line));
        }
        result.anchor = parsePoint(value, line);
      }
    } else {
      result.value = parseValue(part, line);
    }
  }

  return result;
}

function parsePoint(value: Value[], line: ParsedLine): [number, number] {
  if (value.length !== 2 || typeof value[0] !== "number" || typeof value[1] !== "number") {
    throw new ParseError("invalid_point", "Expected a two-number point.", spanForLine(line));
  }
  return [value[0], value[1]];
}

function splitTrailingObject(text: string): [string, string | undefined] {
  if (!text.endsWith("}")) {
    return [text.trim(), undefined];
  }

  let depth = 0;
  let inString = false;
  for (let index = text.length - 1; index >= 0; index -= 1) {
    const char = text[index];
    if (char === '"' && text[index - 1] !== "\\") {
      inString = !inString;
    }
    if (inString) {
      continue;
    }
    if (char === "}") {
      depth += 1;
    } else if (char === "{") {
      depth -= 1;
      if (depth === 0) {
        const before = text.slice(0, index).trimEnd();
        if (before.endsWith(")")) {
          return [text.trim(), undefined];
        }
        return [before, text.slice(index).trim()];
      }
    }
  }

  return [text.trim(), undefined];
}

function isPinLine(text: string): boolean {
  return /^[A-Za-z_@][A-Za-z0-9_@]*\.[A-Za-z_][A-Za-z0-9_]*:/.test(text);
}

function isCall(value: Expr): value is Call {
  return isPlainObject(value) && value.kind === "call";
}

function isPlainObject(value: Value | Expr): value is Record<string, Value> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function unwrap(text: string, open: string, close: string, line: ParsedLine): string {
  const trimmed = text.trim();
  if (!trimmed.startsWith(open) || !trimmed.endsWith(close)) {
    throw new ParseError("invalid_delimiters", `Expected ${open}...${close}.`, spanForLine(line));
  }
  return trimmed.slice(1, -1);
}

function splitTopLevel(text: string, delimiter: string): string[] {
  const result: string[] = [];
  let start = 0;
  let depth = 0;
  let inString = false;

  for (let index = 0; index < text.length; index += 1) {
    const char = text[index];
    if (char === '"' && text[index - 1] !== "\\") {
      inString = !inString;
    }
    if (inString) {
      continue;
    }
    if (char === "{" || char === "[" || char === "(") {
      depth += 1;
    } else if (char === "}" || char === "]" || char === ")") {
      depth -= 1;
    } else if (char === delimiter && depth === 0) {
      result.push(text.slice(start, index).trim());
      start = index + 1;
    }
  }

  result.push(text.slice(start).trim());
  return result.filter((part) => part !== "");
}

function findTopLevel(text: string, needle: string): number {
  let depth = 0;
  let inString = false;

  for (let index = 0; index < text.length; index += 1) {
    const char = text[index];
    if (char === '"' && text[index - 1] !== "\\") {
      inString = !inString;
    }
    if (inString) {
      continue;
    }
    if (char === "{" || char === "[" || char === "(") {
      depth += 1;
    } else if (char === "}" || char === "]" || char === ")") {
      depth -= 1;
    } else if (char === needle && depth === 0) {
      return index;
    }
  }

  return -1;
}

function errorResult(code: string, message: string): ObjectResult {
  return { diagnostics: [{ severity: "error", code, message }] };
}

function spanForLine(line: ParsedLine): SourceSpan {
  return { line: line.line, column: 1, length: line.text.length };
}

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}

class ParseError extends Error {
  constructor(
    public readonly code: string,
    message: string,
    public readonly span?: SourceSpan,
  ) {
    super(message);
  }
}
