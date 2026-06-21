import type { Call, Expr, Ref, Value } from "../index.js";
import { findTopLevel, ParseError, type ParsedLine, spanForLine, splitTopLevel, unwrap } from "./text.js";

export function parseExpr(text: string, line: ParsedLine): Expr {
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

export function tryParseCall(text: string, line: ParsedLine): Call | undefined {
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

export function tryParseRef(text: string): Ref | undefined {
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

export function parsePoint(value: Value[], line: ParsedLine): [number, number] {
  if (value.length !== 2 || typeof value[0] !== "number" || typeof value[1] !== "number") {
    throw new ParseError("invalid_point", "Expected a two-number point.", spanForLine(line));
  }
  return [value[0], value[1]];
}

export function formatExpr(expr: Expr): string {
  if (isCall(expr)) {
    return formatCall(expr);
  }
  if (isRef(expr)) {
    return formatRef(expr);
  }
  return formatValue(expr);
}

export function formatCall(call: Call): string {
  return `${call.callee}(${formatArgList(call.args)})`;
}

export function formatArgList(args: Record<string, Expr>): string {
  return Object.entries(args)
    .map(([key, value]) => `${key}: ${formatExpr(value)}`)
    .join(", ");
}

export function formatRef(ref: Ref): string {
  switch (ref.kind) {
    case "local":
      return ref.name;
    case "member":
      return `${ref.object}.${ref.member}`;
    case "id":
      return `@${ref.id}`;
    default:
      return assertNever(ref);
  }
}

export function formatValue(value: Value): string {
  if (value === null || typeof value === "boolean" || typeof value === "number") {
    return String(value);
  }
  if (typeof value === "string") {
    return JSON.stringify(value);
  }
  if (Array.isArray(value)) {
    return `[${value.map(formatValue).join(", ")}]`;
  }
  if (isName(value)) {
    return value.name;
  }
  return `{${Object.entries(value).map(([key, item]) => `${key}: ${formatValue(item)}`).join(", ")}}`;
}

export function localRef(name: string): Ref {
  return { kind: "local", name };
}

export function nameValue(name: string): Value {
  return { kind: "name", name };
}

export function isCall(value: unknown): value is Call {
  return hasKind(value, "call");
}

export function isName(value: unknown): value is { kind: "name"; name: string } {
  return hasKind(value, "name");
}

export function isLocalRef(value: unknown): value is { kind: "local"; name: string } {
  return hasKind(value, "local");
}

export function isRef(value: unknown): value is Ref {
  return hasKind(value, "local") || hasKind(value, "member") || hasKind(value, "id");
}

export function symbolName(value: Expr | undefined): string | undefined {
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

function hasKind(value: unknown, kind: string): value is { kind: string } {
  return (
    typeof value === "object" &&
    value !== null &&
    !Array.isArray(value) &&
    "kind" in value &&
    value.kind === kind
  );
}

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}
