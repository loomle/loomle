import type { Binding } from "../index.js";
import { parseExpr } from "./expr.js";
import { findTopLevel, ParseError, type ParsedLine, spanForLine } from "./text.js";

export function tryParseBinding(line: ParsedLine): Binding | undefined {
  const eq = findTopLevel(line.text, "=");
  if (eq < 0 || line.text.startsWith("set ")) {
    return undefined;
  }
  return {
    target: parseBindingTarget(line.text.slice(0, eq).trim(), line),
    value: parseExpr(line.text.slice(eq + 1), line),
  };
}

export function parseBindingTarget(text: string, line: ParsedLine): Binding["target"] {
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

export function formatBindingTarget(target: Binding["target"]): string {
  switch (target.kind) {
    case "local":
      return target.name;
    case "member":
      return `${target.object}.${target.member}`;
    default:
      return assertNever(target);
  }
}

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}
