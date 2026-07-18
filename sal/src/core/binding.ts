import type { Binding, BindingTarget } from "../index.js";
import { formatExpr, formatRef, isLocalIdentifier, parseExpr } from "./expr.js";
import { findTopLevel, ParseError, type ParsedLine, spanForLine } from "./text.js";

export function tryParseBinding(
  line: ParsedLine,
  aliases: ReadonlySet<string> = new Set(),
): Binding | undefined {
  if (line.kind !== "code" || line.text.startsWith("set ")) {
    return undefined;
  }
  const eq = findTopLevel(line.text, "=");
  if (eq < 0) {
    return undefined;
  }
  return {
    target: parseBindingTarget(line.text.slice(0, eq).trim(), line),
    value: parseExpr(line.text.slice(eq + 1), line, aliases),
  };
}

export function parseBindingTarget(text: string, line: ParsedLine): BindingTarget {
  const parts = text.trim().split(".");
  if (parts.length === 0 || !isLocalIdentifier(parts[0]) || !parts.slice(1).every((part) => /^[A-Za-z_][A-Za-z0-9_]*$/.test(part))) {
    throw new ParseError("language.invalid_binding_target", "Expected a local name or local member path.", spanForLine(line));
  }
  const object = { kind: "local" as const, name: parts[0] };
  return parts.length === 1
    ? object
    : { kind: "member", object, path: parts.slice(1) as [string, ...string[]] };
}

export function formatBinding(binding: Binding): string {
  return `${formatBindingTarget(binding.target)} = ${formatExpr(binding.value)}`;
}

export function formatBindingTarget(target: BindingTarget): string {
  return target.kind === "local" ? target.name : formatRef(target);
}
