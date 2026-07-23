import type { Binding, BindingTarget } from "../index.js";
import { formatExpr, formatRef, parseExpr, tryParseRef } from "./expr.js";
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
  const ref = tryParseRef(text, new Set(), true);
  if (ref && "name" in ref) {
    return ref;
  }
  if (ref && "object" in ref && "name" in ref.object) {
    return { kind: "member", object: ref.object, path: ref.path };
  }
  throw new ParseError("language.invalid_binding_target", "Expected a local name or local member path.", spanForLine(line));
}

export function formatBinding(binding: Binding): string {
  return `${formatBindingTarget(binding.target)} = ${formatExpr(binding.value)}`;
}

export function formatBindingTarget(target: BindingTarget): string {
  return target.kind === "local" ? target.name : formatRef(target);
}
