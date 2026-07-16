import type {
  Binding,
  Call,
  Expr,
  LocalRef,
  MemberRef,
  ObjectText,
  Page,
  ParseResult,
  Patch,
  PatchOperation,
  PatchStatement,
  Query,
  QueryOperation,
  Ref,
  StableRef,
  Target,
} from "./index.js";
import { parseBindingTarget, tryParseBinding } from "./core/binding.js";
import { parseCondition, parseDetails, parseOrderBy, parsePage } from "./core/condition.js";
import { isCall, isLocalIdentifier, isLocalRef, isName, isRef, parseExpr, parsePoint, parseRef, tryParseCall } from "./core/expr.js";
import { findTopLevel, ParseError, type ParsedLine, preprocessLines, spanForLine, splitTopLevelExact, unwrap } from "./core/text.js";

const collectionKinds = new Set([
  "assets",
  "variables",
  "dispatchers",
  "graphs",
  "components",
  "nodes",
  "properties",
  "functions",
  "defaults",
  "widgets",
]);

const namedKinds = new Set([
  "variable",
  "dispatcher",
  "graph",
  "component",
  "property",
  "function",
  "default",
  "widget",
]);

const idKinds = new Set([
  "blueprint",
  "variable",
  "dispatcher",
  "graph",
  "component",
  "node",
  "pin",
  "widget",
]);

export function parseSalObject(text: string): ParseResult {
  try {
    const lines = preprocessLines(text);
    if (lines.length === 0) {
      return { object: { statements: [] }, diagnostics: [] };
    }
    const requestIndex = lines.findIndex(
      (line) => line.kind === "code" && (line.text.startsWith("query ") || line.text.startsWith("patch ")),
    );
    if (requestIndex < 0) {
      return { object: parseObjectText(lines), diagnostics: [] };
    }

    const { bindings } = parseLeadingBindings(lines.slice(0, requestIndex));
    const header = lines[requestIndex];
    if (header.text.startsWith("query ")) {
      return { object: parseQuery(lines, requestIndex, bindings), diagnostics: [] };
    }
    return { object: parsePatch(lines, requestIndex, bindings), diagnostics: [] };
  } catch (error) {
    if (error instanceof ParseError) {
      return {
        diagnostics: [{ severity: "error", code: error.code, message: error.message, span: error.span }],
      };
    }
    throw error;
  }
}

function parseLeadingBindings(lines: ParsedLine[]): {
  bindings: Map<string, Binding>;
  aliases: Set<string>;
} {
  const bindings = new Map<string, Binding>();
  const aliases = new Set<string>();
  for (const line of lines) {
    if (line.kind === "comment") {
      continue;
    }
    const binding = tryParseBinding(line, aliases);
    if (!binding || binding.target.kind !== "local") {
      throw new ParseError("language.invalid_target_binding", "Only local bindings may precede Query or Patch.", spanForLine(line));
    }
    if (bindings.has(binding.target.name)) {
      throw new ParseError("language.duplicate_binding", `Duplicate binding ${binding.target.name}.`, spanForLine(line));
    }
    bindings.set(binding.target.name, binding);
    aliases.add(binding.target.name);
  }
  return { bindings, aliases };
}

function parseTarget(
  alias: string,
  bindings: Map<string, Binding>,
  line: ParsedLine,
  allowRoot: boolean,
): Target {
  const binding = bindings.get(alias);
  if (!binding) {
    if (allowRoot && isLocalIdentifier(alias)) {
      return { alias, value: { kind: "name", name: alias } };
    }
    throw new ParseError("language.unknown_target", `Target ${alias} has no locator binding.`, spanForLine(line));
  }
  const value = resolveTargetValue(binding.value, bindings, new Set([alias]), line);
  if (!isCall(value) && !isName(value)) {
    throw new ParseError("language.invalid_target", "A target must resolve to a Call or collection-root Name.", spanForLine(line));
  }
  if (!allowRoot && isName(value)) {
    throw new ParseError("language.invalid_patch_target", "Patch requires a bound object Call.", spanForLine(line));
  }
  return { alias, value };
}

function resolveTargetValue(
  value: Expr,
  bindings: Map<string, Binding>,
  stack: Set<string>,
  line: ParsedLine,
): Expr {
  if (isLocalRef(value)) {
    const binding = bindings.get(value.name);
    if (!binding) {
      throw new ParseError("language.unknown_target_dependency", `Unknown target dependency ${value.name}.`, spanForLine(line));
    }
    if (stack.has(value.name)) {
      throw new ParseError("language.cyclic_target", `Cyclic target dependency ${value.name}.`, spanForLine(line));
    }
    const next = new Set(stack);
    next.add(value.name);
    return resolveTargetValue(binding.value, bindings, next, line);
  }
  if (isCall(value)) {
    return {
      ...value,
      args: Object.fromEntries(
        Object.entries(value.args).map(([key, item]) => [key, resolveTargetValue(item, bindings, stack, line)]),
      ),
    };
  }
  if (Array.isArray(value)) {
    return value.map((item) => resolveTargetValue(item, bindings, stack, line));
  }
  if (isRef(value)) {
    if ("object" in value && isLocalRef(value.object)) {
      throw new ParseError(
        "language.unresolved_target_member",
        `Target locator cannot carry document-local member ${value.object.name}.${value.path.join(".")}.`,
        spanForLine(line),
      );
    }
    return value;
  }
  if (value !== null && typeof value === "object" && !isName(value)) {
    return Object.fromEntries(
      Object.entries(value).map(([key, item]) => [key, resolveTargetValue(item, bindings, stack, line)]),
    );
  }
  return value;
}

function parseQuery(
  lines: ParsedLine[],
  queryIndex: number,
  bindings: Map<string, Binding>,
): Query {
  const header = lines[queryIndex];
  const match = /^query\s+([A-Za-z_][A-Za-z0-9_]*)$/.exec(header.text);
  if (!match || !isLocalIdentifier(match[1])) {
    throw new ParseError("language.invalid_query_header", "Expected query <target>.", spanForLine(header));
  }
  const body = lines.slice(queryIndex + 1).filter((line) => line.kind === "code");
  if (body.length === 0) {
    throw new ParseError("language.missing_query_operation", "Query requires one primary operation.", spanForLine(header));
  }

  const query: Query = {
    kind: "query",
    target: parseTarget(match[1], bindings, header, true),
    operation: parseQueryOperation(body[0]),
  };
  const aliases = new Set([match[1]]);
  for (const line of body.slice(1)) {
    if (line.text.startsWith("where ")) {
      if (query.where) duplicateClause("where", line);
      query.where = parseCondition(line.text.slice(6), line, aliases);
    } else if (line.text.startsWith("with ")) {
      if (query.with) duplicateClause("with", line);
      query.with = parseDetails(line.text.slice(5), line) as NonNullable<Query["with"]>;
    } else if (line.text.startsWith("order by ")) {
      if (query.orderBy) duplicateClause("order by", line);
      query.orderBy = parseOrderBy(line.text.slice(9), line) as NonNullable<Query["orderBy"]>;
    } else if (line.text.startsWith("page ")) {
      query.page = mergePage(query.page, parsePage(line), line);
    } else {
      throw new ParseError("language.unexpected_query_statement", `Unexpected Query statement: ${line.text}`, spanForLine(line));
    }
  }
  return query;
}

function parseQueryOperation(line: ParsedLine): QueryOperation {
  const text = line.text;
  if (text === "summary") {
    return { kind: "summary" };
  }
  if (text.startsWith("exec flow ") || text.startsWith("data flow ")) {
    const match = /^(exec|data)\s+flow\s+(from|to)\s+(\S+?)(?:\s+depth\s+(\d+))?$/.exec(text);
    if (!match) operationError(line);
    return {
      kind: match![1] === "exec" ? "exec_flow" : "data_flow",
      direction: match![2] as "from" | "to",
      target: stableRef(match![3], line),
      ...(match![4] ? { depth: positiveInteger(match![4], line) } : {}),
    };
  }
  if (text.startsWith("context ")) {
    const match = /^context\s+(\S+?)(?:\s+depth\s+(\d+))?$/.exec(text);
    if (!match) operationError(line);
    return {
      kind: "context",
      target: stableRef(match![1], line),
      ...(match![2] ? { depth: positiveInteger(match![2], line) } : {}),
    };
  }
  if (text === "tree" || text.startsWith("tree ")) {
    let rest = text.slice(4).trim();
    let root: StableRef | undefined;
    let depth: number | undefined;
    if (rest !== "" && !rest.startsWith("depth ")) {
      const space = rest.indexOf(" ");
      const token = space < 0 ? rest : rest.slice(0, space);
      root = stableRef(token, line);
      rest = space < 0 ? "" : rest.slice(space + 1).trim();
    }
    if (rest !== "") {
      const depthMatch = /^depth\s+(\d+)$/.exec(rest);
      if (!depthMatch) operationError(line);
      depth = positiveInteger(depthMatch![1], line);
    }
    return { kind: "tree", ...(root ? { root } : {}), ...(depth ? { depth } : {}) };
  }
  if (text === "palette entries" || text.startsWith("palette entries ")) {
    let rest = text.slice("palette entries".length).trim();
    let pinContext: { direction: "from" | "to"; pin: StableRef } | undefined;
    const contextMatch = /(?:^|\s)(from|to)\s+(\S+)$/.exec(rest);
    if (contextMatch) {
      pinContext = { direction: contextMatch[1] as "from" | "to", pin: stableRef(contextMatch[2], line) };
      rest = rest.slice(0, contextMatch.index).trim();
    }
    const search = rest === "" ? undefined : quotedText(rest, line);
    return { kind: "palette_entries", ...(search ? { text: search } : {}), ...(pinContext ? { pinContext } : {}) };
  }
  const palette = /^palette\s+@(\S+)$/.exec(text);
  if (palette) {
    return { kind: "palette", id: palette[1] };
  }
  const exact = /^([A-Za-z_][A-Za-z0-9_]*)@([^\s]+)$/.exec(text);
  if (exact && idKinds.has(exact[1])) {
    return { kind: exact[1], id: exact[2] } as QueryOperation;
  }
  const firstSpace = text.indexOf(" ");
  const kind = firstSpace < 0 ? text : text.slice(0, firstSpace);
  const rest = firstSpace < 0 ? "" : text.slice(firstSpace + 1).trim();
  if (collectionKinds.has(kind)) {
    return { kind, ...(rest ? { text: quotedText(rest, line) } : {}) } as QueryOperation;
  }
  if (namedKinds.has(kind) && rest !== "") {
    return { kind, name: exactName(rest, line) } as QueryOperation;
  }
  return operationError(line);
}

function parsePatch(
  lines: ParsedLine[],
  patchIndex: number,
  bindings: Map<string, Binding>,
): Patch {
  const header = lines[patchIndex];
  const match = /^patch\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+(dry run))?$/.exec(header.text);
  if (!match || !isLocalIdentifier(match[1])) {
    throw new ParseError("language.invalid_patch_header", "Expected patch <target> [dry run].", spanForLine(header));
  }
  const aliases = new Set([match[1]]);
  const bindingTargets = new Set<string>();
  const statements: PatchStatement[] = [];
  for (const line of lines.slice(patchIndex + 1)) {
    if (line.kind === "comment") {
      continue;
    }
    const binding = tryParseBinding(line, aliases);
    if (binding) {
      assertBindingOwnerKnown(binding.target, aliases, line);
      const key = bindingTargetKey(binding.target);
      if (bindingTargets.has(key)) {
        throw new ParseError("language.duplicate_binding", `Duplicate binding ${key}.`, spanForLine(line));
      }
      if (binding.target.kind === "local") {
        if (aliases.has(binding.target.name)) {
          throw new ParseError("language.duplicate_binding", `Duplicate binding ${binding.target.name}.`, spanForLine(line));
        }
        aliases.add(binding.target.name);
      }
      bindingTargets.add(key);
      statements.push(binding);
      continue;
    }
    const parsed = parsePatchOperation(line, aliases);
    for (const statement of parsed.statements) {
      if (!isBindingStatement(statement)) {
        assertPatchRefsKnown(statement, aliases, line);
      }
    }
    statements.push(...parsed.statements);
    for (const alias of parsed.outputs) {
      if (aliases.has(alias)) {
        throw new ParseError("language.duplicate_binding", `Duplicate binding ${alias}.`, spanForLine(line));
      }
      aliases.add(alias);
    }
  }
  if (statements.length === 0) {
    throw new ParseError("language.missing_patch_statement", "Patch requires at least one binding or operation.", spanForLine(header));
  }
  return {
    kind: "patch",
    target: parseTarget(match[1], bindings, header, false),
    dryRun: Boolean(match[2]),
    statements: statements as Patch["statements"],
  };
}

function parsePatchOperation(
  line: ParsedLine,
  aliases: ReadonlySet<string>,
): { statements: PatchStatement[]; outputs: string[] } {
  const text = line.text;
  if (text === "save" || text === "compile") {
    return { statements: [{ kind: text } as PatchOperation], outputs: [] };
  }
  if (text.startsWith("add ")) {
    return { statements: parseAdd(text.slice(4), line), outputs: [] };
  }
  if (text.startsWith("remove ")) {
    return simpleTarget("remove", text.slice(7), line);
  }
  if (text.startsWith("break ")) {
    return simpleTarget("break", text.slice(6), line);
  }
  if (text.startsWith("set ")) {
    const body = text.slice(4);
    const eq = findTopLevel(body, "=");
    if (eq < 0) operationError(line);
    const target = memberRef(body.slice(0, eq), line);
    return { statements: [{ kind: "set", target, value: parseExpr(body.slice(eq + 1), line, aliases) }], outputs: [] };
  }
  if (text.startsWith("reset ")) {
    return { statements: [{ kind: "reset", target: memberRef(text.slice(6), line) }], outputs: [] };
  }
  if (text.startsWith("move ")) {
    return { statements: [parseMove(text.slice(5), line)], outputs: [] };
  }
  if (text.startsWith("connect ") || text.startsWith("disconnect ")) {
    const kind = text.startsWith("connect ") ? "connect" : "disconnect";
    const body = text.slice(kind.length + 1);
    const edge = parseEdge(body, line);
    return { statements: [{ kind, ...edge }], outputs: [] };
  }
  if (text.startsWith("insert ")) {
    const parts = splitTopLevelExact(text.slice(7), "->");
    if (parts.length !== 3) operationError(line);
    const middle = splitTopLevelExact(parts[1], "/");
    if (middle.length !== 2) operationError(line);
    return {
      statements: [{
        kind: "insert",
        from: parseRef(parts[0], line),
        input: parseRef(middle[0], line),
        output: parseRef(middle[1], line),
        to: parseRef(parts[2], line),
      }],
      outputs: [],
    };
  }
  if (text.startsWith("wrap ")) {
    return { statements: [parseWrap(text.slice(5), line)], outputs: [] };
  }
  if (text.startsWith("replace ")) {
    const match = /^(.+)\s+with\s+([A-Za-z_][A-Za-z0-9_]*)$/.exec(text.slice(8));
    if (!match) operationError(line);
    return {
      statements: [{ kind: "replace", target: parseRef(match![1], line), with: localOutput(match![2], line) }],
      outputs: [],
    };
  }
  if (text.startsWith("invoke ")) {
    return parseInvoke(text.slice(7), line, aliases);
  }
  return operationError(line);
}

function parseAdd(text: string, line: ParsedLine): PatchStatement[] {
  const space = text.indexOf(" ");
  const targetText = space < 0 ? text : text.slice(0, space);
  const rest = space < 0 ? "" : text.slice(space + 1).trim();
  const target = parseBindingTarget(targetText, line);
  const add: PatchOperation = { kind: "add", target };
  if (rest === "") {
    return [add];
  }
  const placement = /^(to|before|after)\s+(.+)$/.exec(rest);
  if (placement) {
    return [{ ...add, [placement[1]]: parseRef(placement[2], line) } as PatchOperation];
  }
  return [add, { kind: "connect", ...parseEdge(rest, line) }];
}

function simpleTarget(kind: "remove" | "break", text: string, line: ParsedLine) {
  return { statements: [{ kind, target: parseRef(text, line) } as PatchOperation], outputs: [] };
}

function parseMove(text: string, line: ParsedLine): PatchOperation {
  const match = /^(\S+)\s+(to|by|before|after)\s+(.+)$/.exec(text);
  if (!match) return operationError(line);
  const target = parseRef(match[1], line);
  if (match[2] === "by") {
    return { kind: "move", target, by: parsePoint(match[3], line) };
  }
  if (match[2] === "to" && (match[3].trim().startsWith("(") || match[3].trim().startsWith("["))) {
    return { kind: "move", target, to: parsePoint(match[3], line) };
  }
  return { kind: "move", target, [match[2]]: parseRef(match[3], line) } as PatchOperation;
}

function parseWrap(text: string, line: ParsedLine): PatchOperation {
  const match = /^(.+)\s+with\s+([A-Za-z_][A-Za-z0-9_]*)$/.exec(text);
  if (!match) return operationError(line);
  const source = match[1].trim();
  const targets = source.startsWith("[")
    ? splitTopLevelExact(unwrap(source, "[", "]", line), ",").map((item) => parseRef(item, line))
    : [parseRef(source, line)];
  return { kind: "wrap", targets: targets as [Ref, ...Ref[]], with: localOutput(match[2], line) };
}

function parseInvoke(
  text: string,
  line: ParsedLine,
  aliases: ReadonlySet<string>,
): { statements: PatchStatement[]; outputs: string[] } {
  const space = text.indexOf(" ");
  if (space < 0) return operationError(line);
  const target = parseRef(text.slice(0, space), line);
  const remainder = text.slice(space + 1).trim();
  const asIndex = findTopLevel(remainder, " as ");
  const callText = asIndex < 0 ? remainder : remainder.slice(0, asIndex);
  const outputText = asIndex < 0 ? "" : remainder.slice(asIndex + 4).trim();
  const call = tryParseCall(callText, line, aliases);
  if (!call) return operationError(line);
  const outputs = outputText === "" ? [] : splitTopLevelExact(outputText, ",").map((item) => {
    const colon = findTopLevel(item, ":");
    const selector = colon < 0 ? undefined : item.slice(0, colon).trim();
    const alias = (colon < 0 ? item : item.slice(colon + 1)).trim();
    if (!isLocalIdentifier(alias) || (selector !== undefined && !/^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*$/.test(selector))) {
      throw new ParseError("language.invalid_invoke_output", "Invalid invoke output binding.", spanForLine(line));
    }
    return { ...(selector ? { selector } : {}), alias };
  });
  return {
    statements: [{ kind: "invoke", target, operation: call.callee, args: call.args, outputs }],
    outputs: outputs.map((output) => output.alias),
  };
}

function parseObjectText(lines: ParsedLine[]): ObjectText {
  const statements: ObjectText["statements"] = [];
  const aliases = new Set<string>();
  const bindingTargets = new Set<string>();
  for (const line of lines) {
    if (line.kind === "comment") {
      statements.push({ kind: "comment", text: line.text });
      continue;
    }
    const binding = tryParseBinding(line, aliases);
    if (binding) {
      assertBindingOwnerKnown(binding.target, aliases, line);
      const key = bindingTargetKey(binding.target);
      if (bindingTargets.has(key)) {
        throw new ParseError("language.duplicate_binding", `Duplicate binding ${key}.`, spanForLine(line));
      }
      if (binding.target.kind === "local" && aliases.has(binding.target.name)) {
        throw new ParseError("language.duplicate_binding", `Duplicate binding ${binding.target.name}.`, spanForLine(line));
      }
      statements.push(binding);
      bindingTargets.add(key);
      if (binding.target.kind === "local") {
        aliases.add(binding.target.name);
      }
      continue;
    }
    const parts = splitTopLevelExact(line.text, "->");
    if (parts.length === 2) {
      const from = parseRef(parts[0], line);
      const to = parseRef(parts[1], line);
      assertRefKnown(from, aliases, line);
      assertRefKnown(to, aliases, line);
      statements.push({ from, to });
      continue;
    }
    throw new ParseError("language.invalid_object_statement", `Unsupported Object Text statement: ${line.text}`, spanForLine(line));
  }
  return { statements };
}

function parseEdge(text: string, line: ParsedLine): { from: Ref; to: Ref } {
  const parts = splitTopLevelExact(text, "->");
  if (parts.length !== 2) return operationError(line);
  return { from: parseRef(parts[0], line), to: parseRef(parts[1], line) };
}

function memberRef(text: string, line: ParsedLine): MemberRef {
  const ref = parseRef(text.trim(), line);
  if (!("object" in ref)) {
    throw new ParseError("language.expected_member", "Expected an object field or member path.", spanForLine(line));
  }
  return ref;
}

function assertPatchRefsKnown(
  operation: PatchOperation,
  aliases: ReadonlySet<string>,
  line: ParsedLine,
): void {
  switch (operation.kind) {
    case "add":
      assertBindingTargetKnown(operation.target, aliases, line);
      if (operation.to) assertRefKnown(operation.to, aliases, line);
      if (operation.before) assertRefKnown(operation.before, aliases, line);
      if (operation.after) assertRefKnown(operation.after, aliases, line);
      return;
    case "remove":
    case "break":
      assertRefKnown(operation.target, aliases, line);
      return;
    case "set":
    case "reset":
      assertRefKnown(operation.target, aliases, line);
      return;
    case "move":
      assertRefKnown(operation.target, aliases, line);
      if (operation.to && !Array.isArray(operation.to)) assertRefKnown(operation.to, aliases, line);
      if (operation.before) assertRefKnown(operation.before, aliases, line);
      if (operation.after) assertRefKnown(operation.after, aliases, line);
      return;
    case "connect":
    case "disconnect":
      assertRefKnown(operation.from, aliases, line);
      assertRefKnown(operation.to, aliases, line);
      return;
    case "insert":
      assertRefKnown(operation.from, aliases, line);
      assertRefKnown(operation.input, aliases, line);
      assertRefKnown(operation.output, aliases, line);
      assertRefKnown(operation.to, aliases, line);
      return;
    case "wrap":
      operation.targets.forEach((target) => assertRefKnown(target, aliases, line));
      assertRefKnown(operation.with, aliases, line);
      return;
    case "replace":
      assertRefKnown(operation.target, aliases, line);
      assertRefKnown(operation.with, aliases, line);
      return;
    case "invoke":
      assertRefKnown(operation.target, aliases, line);
      return;
    case "compile":
    case "save":
      return;
  }
}

function assertBindingTargetKnown(
  target: Binding["target"],
  aliases: ReadonlySet<string>,
  line: ParsedLine,
): void {
  if (target.kind === "local") {
    assertLocalKnown(target.name, aliases, line);
    return;
  }
  assertLocalKnown(target.object.name, aliases, line);
}

function assertBindingOwnerKnown(
  target: Binding["target"],
  aliases: ReadonlySet<string>,
  line: ParsedLine,
): void {
  if (target.kind === "member") {
    assertLocalKnown(target.object.name, aliases, line);
  }
}

function assertRefKnown(ref: Ref, aliases: ReadonlySet<string>, line: ParsedLine): void {
  if (isLocalRef(ref)) {
    assertLocalKnown(ref.name, aliases, line);
  } else if ("object" in ref && isLocalRef(ref.object)) {
    assertLocalKnown(ref.object.name, aliases, line);
  }
}

function assertLocalKnown(name: string, aliases: ReadonlySet<string>, line: ParsedLine): void {
  if (!aliases.has(name)) {
    throw new ParseError("language.unknown_local_reference", `Local alias ${name} has not been declared yet.`, spanForLine(line));
  }
}

function bindingTargetKey(target: Binding["target"]): string {
  return target.kind === "local" ? target.name : `${target.object.name}.${target.path.join(".")}`;
}

function isBindingStatement(statement: PatchStatement): statement is Binding {
  return !(("kind" in statement));
}

function stableRef(text: string, line: ParsedLine): StableRef {
  const ref = parseRef(text, line);
  if (!("id" in ref)) {
    throw new ParseError("language.expected_stable_reference", "Expected a typed object@id reference.", spanForLine(line));
  }
  return ref;
}

function localOutput(name: string, line: ParsedLine): LocalRef {
  if (!isLocalIdentifier(name)) {
    throw new ParseError("language.invalid_binding_target", `Invalid local alias ${name}.`, spanForLine(line));
  }
  return { kind: "local", name };
}

function positiveInteger(text: string, line: ParsedLine): number {
  const value = Number(text);
  if (!Number.isInteger(value) || value < 1) {
    throw new ParseError("language.invalid_depth", "Depth must be a positive integer.", spanForLine(line));
  }
  return value;
}

function quotedText(text: string, line: ParsedLine): string {
  if (!/^"(?:[^"\\]|\\.)*"$/.test(text)) {
    throw new ParseError("language.expected_quoted_text", "Search text must be quoted.", spanForLine(line));
  }
  return parseQuotedString(text, line);
}

function exactName(text: string, line: ParsedLine): string {
  if (/^"(?:[^"\\]|\\.)*"$/.test(text)) {
    return parseQuotedString(text, line);
  }
  if (!/^\S+$/.test(text)) {
    throw new ParseError("language.invalid_exact_name", "Quote exact names containing spaces.", spanForLine(line));
  }
  return text;
}

function parseQuotedString(text: string, line: ParsedLine): string {
  try {
    return JSON.parse(text) as string;
  } catch {
    throw new ParseError("language.invalid_string", "Invalid quoted string.", spanForLine(line));
  }
}

function mergePage(current: Page | undefined, next: Page, line: ParsedLine): Page {
  if ((next.limit !== undefined && current?.limit !== undefined) || (next.after !== undefined && current?.after !== undefined)) {
    duplicateClause("page", line);
  }
  return { ...current, ...next };
}

function duplicateClause(name: string, line: ParsedLine): never {
  throw new ParseError("language.duplicate_query_clause", `Duplicate ${name} clause.`, spanForLine(line));
}

function operationError(line: ParsedLine): never {
  throw new ParseError("language.invalid_operation", `Invalid operation: ${line.text}`, spanForLine(line));
}
