import type { Call, Expr, LocalRef, Name, Ref } from "../index.js";
import { findTopLevel, ParseError, type ParsedLine, spanForLine, splitTopLevelExact, unwrap } from "./text.js";

export function parseExpr(text: string, line: ParsedLine, aliases: ReadonlySet<string> = new Set()): Expr {
  const trimmed = text.trim();
  const call = tryParseCall(trimmed, line, aliases);
  if (call) {
    return call;
  }
  if (trimmed === "null") {
    return null;
  }
  if (trimmed === "true") {
    return true;
  }
  if (trimmed === "false") {
    return false;
  }
  if (/^-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?$/.test(trimmed)) {
    return Number(trimmed);
  }
  if (trimmed.startsWith('"') && trimmed.endsWith('"')) {
    try {
      return JSON.parse(trimmed) as string;
    } catch {
      throw new ParseError("language.invalid_string", "Invalid quoted string.", spanForLine(line));
    }
  }
  if (trimmed.startsWith("[") && trimmed.endsWith("]")) {
    const inner = unwrap(trimmed, "[", "]", line);
    return inner.trim() === "" ? [] : splitTopLevelExact(inner, ",").map((part) => parseExpr(part, line, aliases));
  }
  if (trimmed.startsWith("{") && trimmed.endsWith("}")) {
    return parseObjectLiteral(trimmed, line, aliases);
  }

  const ref = tryParseRef(trimmed, aliases);
  if (ref) {
    return ref;
  }
  if (/^[A-Za-z_][A-Za-z0-9_]*$/.test(trimmed)) {
    return { kind: "name", name: trimmed };
  }
  throw new ParseError("language.unsupported_value", `Unsupported value: ${trimmed}`, spanForLine(line));
}

export function tryParseCall(
  text: string,
  line: ParsedLine,
  aliases: ReadonlySet<string> = new Set(),
): Call | undefined {
  const match = /^([A-Za-z_][A-Za-z0-9_]*)\(([\s\S]*)\)$/.exec(text.trim());
  if (!match) {
    return undefined;
  }
  return { kind: "call", callee: match[1], args: parseCallArgs(match[2], line, aliases) };
}

export function parseCallArgs(
  text: string,
  line: ParsedLine,
  aliases: ReadonlySet<string> = new Set(),
): Record<string, Expr> {
  if (text.trim() === "") {
    return {};
  }
  const result: Record<string, Expr> = {};
  for (const part of splitTopLevelExact(text, ",")) {
    const colon = findTopLevel(part, ":");
    if (colon < 0) {
      throw new ParseError("language.invalid_call_args", "Arguments must use name: value.", spanForLine(line));
    }
    const name = part.slice(0, colon).trim();
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(name)) {
      throw new ParseError("language.invalid_call_args", `Invalid argument name ${name}.`, spanForLine(line));
    }
    if (name in result) {
      throw new ParseError("language.duplicate_argument", `Duplicate argument ${name}.`, spanForLine(line));
    }
    result[name] = parseExpr(part.slice(colon + 1), line, aliases);
  }
  return result;
}

export function parseRef(text: string, line: ParsedLine): Ref {
  const ref = tryParseRef(text, new Set(), true);
  if (!ref) {
    throw new ParseError("language.invalid_reference", `Expected an object reference, received ${text.trim()}.`, spanForLine(line));
  }
  return ref;
}

export function tryParseRef(
  text: string,
  aliases: ReadonlySet<string> = new Set(),
  allowBareLocal = false,
): Ref | undefined {
  const trimmed = text.trim();
  const stable = /^([A-Za-z_][A-Za-z0-9_]*)@([^\.\[\]\s]+)((?:\.[A-Za-z_][A-Za-z0-9_]*|\[\d+\])*)$/.exec(trimmed);
  if (stable) {
    if (!isLocalIdentifier(stable[1])) {
      return undefined;
    }
    const object = { kind: stable[1], id: stable[2] };
    if (!stable[3]) {
      return object;
    }
    const path = parseMemberPath(stable[3]);
    return path ? { kind: "member", object, path } : undefined;
  }

  const local = /^([A-Za-z_][A-Za-z0-9_]*)((?:\.[A-Za-z_][A-Za-z0-9_]*|\[\d+\])*)$/.exec(trimmed);
  if (!local || (!allowBareLocal && !local[2] && !aliases.has(local[1]))) {
    return undefined;
  }
  if (!isLocalIdentifier(local[1])) {
    return undefined;
  }
  const object: LocalRef = { kind: "local", name: local[1] };
  if (!local[2]) {
    return object;
  }
  const path = parseMemberPath(local[2]);
  return path ? { kind: "member", object, path } : undefined;
}

function parseMemberPath(text: string): [string | number, ...(string | number)[]] | undefined {
  const parts: Array<string | number> = [];
  let index = 0;
  while (index < text.length) {
    const identifier = /^\.([A-Za-z_][A-Za-z0-9_]*)/.exec(text.slice(index));
    if (identifier) {
      parts.push(identifier[1]);
      index += identifier[0].length;
      continue;
    }
    const arrayIndex = /^\[(\d+)\]/.exec(text.slice(index));
    if (!arrayIndex) {
      return undefined;
    }
    const value = Number(arrayIndex[1]);
    if (!Number.isSafeInteger(value) || value > 2147483647) {
      return undefined;
    }
    parts.push(value);
    index += arrayIndex[0].length;
  }
  return parts.length > 0
    ? parts as [string | number, ...(string | number)[]]
    : undefined;
}

function parseObjectLiteral(
  text: string,
  line: ParsedLine,
  aliases: ReadonlySet<string>,
): Record<string, Expr> {
  const inner = unwrap(text, "{", "}", line);
  if (inner.trim() === "") {
    return {};
  }
  const result: Record<string, Expr> = {};
  for (const part of splitTopLevelExact(inner, ",")) {
    const colon = findTopLevel(part, ":");
    if (colon < 0) {
      throw new ParseError("language.invalid_object", "Object entries must use key: value.", spanForLine(line));
    }
    const key = part.slice(0, colon).trim();
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(key)) {
      throw new ParseError("language.invalid_object", `Invalid object key ${key}.`, spanForLine(line));
    }
    if (key in result) {
      throw new ParseError("language.duplicate_object_key", `Duplicate object key ${key}.`, spanForLine(line));
    }
    result[key] = parseExpr(part.slice(colon + 1), line, aliases);
  }
  return result;
}

export function parsePoint(text: string, line: ParsedLine): [number, number] {
  const trimmed = text.trim();
  const inner = trimmed.startsWith("(") && trimmed.endsWith(")")
    ? unwrap(trimmed, "(", ")", line)
    : unwrap(trimmed, "[", "]", line);
  const parts = splitTopLevelExact(inner, ",").map((part) => part === "" ? Number.NaN : Number(part));
  if (parts.length !== 2 || parts.some((value) => !Number.isFinite(value))) {
    throw new ParseError("language.invalid_point", "Expected a two-number point.", spanForLine(line));
  }
  return [parts[0], parts[1]];
}

export function formatExpr(expr: Expr): string {
  if (expr === null || typeof expr === "boolean" || typeof expr === "number") {
    return String(expr);
  }
  if (typeof expr === "string") {
    return JSON.stringify(expr);
  }
  if (Array.isArray(expr)) {
    return `[${expr.map(formatExpr).join(", ")}]`;
  }
  if (isCall(expr)) {
    return formatCall(expr);
  }
  if (isName(expr)) {
    return expr.name;
  }
  if (isRef(expr)) {
    return formatRef(expr);
  }
  return `{${Object.entries(expr).map(([key, value]) => `${key}: ${formatExpr(value)}`).join(", ")}}`;
}

export function formatCall(call: Call): string {
  return `${call.callee}(${formatArgList(call.args)})`;
}

export function formatArgList(args: Record<string, Expr>): string {
  return Object.entries(args).map(([key, value]) => `${key}: ${formatExpr(value)}`).join(", ");
}

export function formatRef(ref: Ref): string {
  if (isLocalRef(ref)) {
    return ref.name;
  }
  if ("object" in ref) {
    return `${formatRef(ref.object)}${formatMemberPath(ref.path)}`;
  }
  return `${ref.kind}@${ref.id}`;
}

export function formatMemberPath(path: readonly (string | number)[]): string {
  return path.map((segment) => typeof segment === "number" ? `[${segment}]` : `.${segment}`).join("");
}

export function localRef(name: string): LocalRef {
  return { kind: "local", name };
}

export function nameValue(name: string): Name {
  return { kind: "name", name };
}

export function isLocalIdentifier(value: string): boolean {
  return /^(?!(?:true|false|null)$)[A-Za-z_][A-Za-z0-9_]*$/.test(value);
}

export function isCall(value: unknown): value is Call {
  return hasKind(value, "call") && "callee" in value && "args" in value;
}

export function isName(value: unknown): value is Name {
  return hasKind(value, "name") && "name" in value;
}

export function isLocalRef(value: unknown): value is LocalRef {
  return hasKind(value, "local") && "name" in value;
}

export function isRef(value: unknown): value is Ref {
  if (hasKind(value, "local") && !("id" in value)) {
    return "name" in value;
  }
  if (hasKind(value, "member") && !("id" in value)) {
    return "object" in value && "path" in value;
  }
  return typeof value === "object" && value !== null && "kind" in value && "id" in value;
}

function hasKind(value: unknown, kind: string): value is { kind: string } {
  return typeof value === "object" && value !== null && !Array.isArray(value) && "kind" in value && value.kind === kind;
}
