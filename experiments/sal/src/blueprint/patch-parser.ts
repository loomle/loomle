import type { Binding, BlueprintPatchOp, Expr, FieldPath, Patch } from "../index.js";
import { tryParseBinding } from "../core/binding.js";
import { parseExpr } from "../core/expr.js";
import { ParseError, type ParsedLine, spanForLine } from "../core/text.js";
import type { BlueprintBinding } from "./parser.js";

export function parseBlueprintPatch(
  lines: ParsedLine[],
  patchIndex: number,
  bindings: Map<string, BlueprintBinding>,
): Patch {
  const patchLine = lines[patchIndex];
  const match = /^patch\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+(dry)\s+(run))?$/.exec(patchLine.text);
  if (!match) {
    throw new ParseError("invalid_patch", "Expected patch <blueprint>.", spanForLine(patchLine));
  }
  const binding = bindings.get(match[1]);
  if (!binding) {
    throw new ParseError("unknown_blueprint_binding", `Unknown blueprint binding ${match[1]}.`, spanForLine(patchLine));
  }

  const patch: Patch = {
    kind: "patch",
    target: { domain: "blueprint", asset: binding.asset },
    dryRun: match[2] === "dry" && match[3] === "run",
    bindings: [],
    ops: [],
  };

  for (const line of lines.slice(patchIndex + 1)) {
    const addBinding = tryParseAddBinding(line);
    if (addBinding) {
      patch.bindings.push(addBinding.binding);
      patch.ops.push({ kind: "add", target: addBinding.target });
      continue;
    }

    const binding = tryParseBinding(line);
    if (binding) {
      patch.bindings.push(binding);
      continue;
    }

    patch.ops.push(parseBlueprintOp(line));
  }

  return patch;
}

function tryParseAddBinding(line: ParsedLine): { target: FieldPath; binding: Binding } | undefined {
  const match = /^add\s+(.+)$/.exec(line.text);
  if (!match || !match[1].includes("=")) {
    return undefined;
  }
  const synthetic = { text: match[1], line: line.line };
  const binding = tryParseBinding(synthetic);
  if (!binding) {
    throw new ParseError("invalid_add_binding", "add binding sugar requires add target = constructor(...).", spanForLine(line));
  }
  return { target: fieldPathFromBinding(binding), binding };
}

function parseBlueprintOp(line: ParsedLine): BlueprintPatchOp {
  let match = /^add\s+([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)$/.exec(line.text);
  if (match) {
    return {
      kind: "add",
      target: parseFieldPath(match[1], line),
    };
  }

  match = /^set\s+([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)\s*=\s*(.+)$/.exec(line.text);
  if (match) {
    return {
      kind: "set",
      target: parseFieldPath(match[1], line),
      value: parseExpr(match[2], line) as Expr,
    };
  }

  match = /^remove\s+([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)$/.exec(line.text);
  if (match) {
    return {
      kind: "remove",
      target: parseFieldPath(match[1], line),
    };
  }

  match = /^rename\s+([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)\s+to\s+([A-Za-z_][A-Za-z0-9_]*)$/.exec(line.text);
  if (match) {
    return {
      kind: "rename",
      target: parseFieldPath(match[1], line),
      name: match[2],
    };
  }

  match = /^move\s+([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)\s+(before|after)\s+([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)$/.exec(line.text);
  if (match) {
    return {
      kind: "move",
      target: parseFieldPath(match[1], line),
      position: match[2] as "before" | "after",
      relativeTo: parseFieldPath(match[3], line),
    };
  }

  throw new ParseError("unsupported_blueprint_patch_op", "Expected add binding, set target = value, remove target, rename target to name, or move target before/after other.", spanForLine(line));
}

function fieldPathFromBinding(binding: Binding): FieldPath {
  switch (binding.target.kind) {
    case "local":
      return { path: [binding.target.name] };
    case "member":
      return { path: [binding.target.object, binding.target.member] };
    default:
      return assertNever(binding.target);
  }
}

function parseFieldPath(text: string, line: ParsedLine): FieldPath {
  const path = text.split(".");
  if (path.some((part) => part.length === 0)) {
    throw new ParseError("invalid_field_path", "Expected a dotted field path.", spanForLine(line));
  }
  return { path: path as [string, ...string[]] };
}

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}
