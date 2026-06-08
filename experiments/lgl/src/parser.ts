import type {
  AddStatement,
  ConnectStatement,
  Document,
  EdgeStatement,
  GraphStatement,
  Literal,
  NodeDeclaration,
  PatchStatement,
  PinRef,
  RewireStatement,
  SetStatement,
  SourceSpan,
  UseBinding
} from "./ir.js";

export class LglParseError extends Error {
  constructor(message: string, readonly span: SourceSpan) {
    super(`${message} at ${span.line}:${span.column}`);
    this.name = "LglParseError";
  }
}

export function parseLgl(source: string): Document {
  const lines = source.replace(/\r\n/g, "\n").split("\n");
  const headerIndex = lines.findIndex((line) => /^(graph|patch)\s+/.test(meaningful(line)));
  if (headerIndex < 0) {
    throw new LglParseError("Expected graph or patch header", { line: 1, column: 1 });
  }

  const header = meaningful(lines[headerIndex]);
  const headerMatch = /^(graph|patch)\s+([A-Za-z_][A-Za-z0-9_.-]*)$/.exec(header);
  if (!headerMatch) {
    throw new LglParseError("Invalid document header", { line: headerIndex + 1, column: 1 });
  }

  const [, kind, name] = headerMatch;
  const statements: Array<GraphStatement | PatchStatement> = [];
  const bindings: UseBinding[] = [];

  for (let index = 0; index < lines.length; index += 1) {
    if (index === headerIndex) {
      continue;
    }
    const raw = lines[index];
    const text = meaningful(raw);
    if (text.length === 0) {
      continue;
    }

    const span = { line: index + 1, column: raw.indexOf(text) + 1 };
    if (text.startsWith("use ")) {
      bindings.push(parseUseBinding(text, span));
      continue;
    }
    if (index < headerIndex) {
      throw new LglParseError("Only use bindings may appear before the document header", span);
    }

    if (kind === "graph") {
      statements.push(parseGraphStatement(text, span));
    } else {
      statements.push(parsePatchStatement(text, span));
    }
  }

  if (kind === "graph") {
    return { kind: "graph", name, bindings, statements: statements as GraphStatement[] };
  }
  return { kind: "patch", name, bindings, statements: statements as PatchStatement[] };
}

function parseUseBinding(text: string, span: SourceSpan): UseBinding {
  const match = /^use\s+([A-Za-z_][A-Za-z0-9_]*)\s+from\s+(palette(?:\s+entry)?)\s+"((?:\\"|[^"])*)"(.*)$/.exec(text);
  if (!match) {
    throw new LglParseError("Invalid use binding", span);
  }

  const [, symbol, sourceKind, rawValue, rest] = match;
  const source =
    sourceKind === "palette"
      ? { kind: "palette" as const, query: JSON.parse(`"${rawValue}"`) as string }
      : { kind: "palette_entry" as const, id: JSON.parse(`"${rawValue}"`) as string };

  return {
    kind: "use",
    symbol,
    source,
    ...parseBindingClauses(rest.trim(), span),
    span
  };
}

function parseBindingClauses(
  text: string,
  span: SourceSpan
): Pick<UseBinding, "context" | "where"> {
  let rest = text;
  let context: UseBinding["context"];
  const where: UseBinding["where"] = [];

  const componentMatch = /\bcontext\s+component\s+"((?:\\"|[^"])*)"/.exec(rest);
  if (componentMatch) {
    context = { kind: "component", name: JSON.parse(`"${componentMatch[1]}"`) as string };
    rest = rest.replace(componentMatch[0], "").trim();
  }

  const fromPinMatch = /\bcontext\s+from\s+([A-Za-z_][A-Za-z0-9_]*\.[A-Za-z_][A-Za-z0-9_]*)/.exec(rest);
  if (fromPinMatch) {
    if (context) {
      throw new LglParseError("Binding may only have one context clause", span);
    }
    context = { kind: "fromPin", pin: parsePinRef(fromPinMatch[1], span) };
    rest = rest.replace(fromPinMatch[0], "").trim();
  }

  const wherePattern = /\bwhere\s+([A-Za-z_][A-Za-z0-9_.-]*)\s*=\s*("[^"]*"|-?\d+(?:\.\d+)?|true|false|null|[A-Za-z_][A-Za-z0-9_.-]*)/g;
  rest = rest.replace(wherePattern, (_full, key: string, value: string) => {
    where.push({ key, value: parseLiteral(value, span) });
    return "";
  }).trim();

  if (rest.length > 0) {
    throw new LglParseError(`Unknown binding clause: ${rest}`, span);
  }

  return {
    ...(context ? { context } : {}),
    ...(where.length > 0 ? { where } : {})
  };
}

function parseGraphStatement(text: string, span: SourceSpan): GraphStatement {
  if (text.includes("->")) {
    return parseEdge(text, span);
  }
  return parseNodeDeclaration(text, span);
}

function parsePatchStatement(text: string, span: SourceSpan): PatchStatement {
  if (text.startsWith("set ")) {
    return parseSet(text, span);
  }
  if (text.startsWith("add ")) {
    return { kind: "add", node: parseNodeDeclaration(text.slice(4).trim(), span), span };
  }
  if (text.startsWith("rewire ")) {
    const edge = parseEdge(text.slice(7).trim(), span);
    return { kind: "rewire", from: edge.from, to: edge.to, span };
  }
  if (text.includes("->")) {
    const edge = parseEdge(text, span);
    const connect: ConnectStatement = { kind: "connect", from: edge.from, to: edge.to, span };
    return connect;
  }
  throw new LglParseError("Unknown patch statement", span);
}

function parseSet(text: string, span: SourceSpan): SetStatement {
  const match = /^set\s+([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+)$/.exec(text);
  if (!match) {
    throw new LglParseError("Invalid set statement", span);
  }
  return {
    kind: "set",
    target: { node: match[1], property: match[2] },
    value: parseLiteral(match[3].trim(), span),
    span
  };
}

function parseNodeDeclaration(text: string, span: SourceSpan): NodeDeclaration {
  const match = /^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([A-Za-z_][A-Za-z0-9_.]*)\((.*)\)$/.exec(text);
  if (!match) {
    throw new LglParseError("Invalid node declaration", span);
  }
  return {
    kind: "node",
    alias: match[1],
    type: match[2],
    args: parseArgs(match[3], span),
    span
  };
}

function parseEdge(text: string, span: SourceSpan): EdgeStatement {
  const parts = text.split("->");
  if (parts.length !== 2) {
    throw new LglParseError("Invalid edge statement", span);
  }
  return {
    kind: "edge",
    from: parsePinRef(parts[0].trim(), span),
    to: parsePinRef(parts[1].trim(), span),
    span
  };
}

function parsePinRef(text: string, span: SourceSpan): PinRef {
  const match = /^([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)$/.exec(text);
  if (!match) {
    throw new LglParseError("Invalid pin reference", span);
  }
  return { node: match[1], pin: match[2] };
}

function parseArgs(text: string, span: SourceSpan): Literal[] {
  const trimmed = text.trim();
  if (trimmed.length === 0) {
    return [];
  }
  return splitArgs(trimmed, span).map((arg) => parseLiteral(arg.trim(), span));
}

function splitArgs(text: string, span: SourceSpan): string[] {
  const args: string[] = [];
  let start = 0;
  let inString = false;
  let escaped = false;

  for (let index = 0; index < text.length; index += 1) {
    const char = text[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (char === "\\") {
      escaped = true;
      continue;
    }
    if (char === "\"") {
      inString = !inString;
      continue;
    }
    if (char === "," && !inString) {
      args.push(text.slice(start, index));
      start = index + 1;
    }
  }

  if (inString) {
    throw new LglParseError("Unterminated string literal", span);
  }
  args.push(text.slice(start));
  return args;
}

function parseLiteral(text: string, span: SourceSpan): Literal {
  if (/^".*"$/.test(text)) {
    return { kind: "string", value: JSON.parse(text) as string };
  }
  if (/^-?\d+(\.\d+)?$/.test(text)) {
    return { kind: "number", value: Number(text) };
  }
  if (text === "true" || text === "false") {
    return { kind: "boolean", value: text === "true" };
  }
  if (text === "null") {
    return { kind: "null", value: null };
  }
  if (/^[A-Za-z_][A-Za-z0-9_.-]*$/.test(text)) {
    return { kind: "identifier", value: text };
  }
  throw new LglParseError("Invalid literal", span);
}

function meaningful(line: string): string {
  const commentIndex = line.indexOf("#");
  return (commentIndex >= 0 ? line.slice(0, commentIndex) : line).trim();
}
