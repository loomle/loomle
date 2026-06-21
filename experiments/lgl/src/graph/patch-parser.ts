import type { Binding, Call, GraphTarget, NodeCreation, Op, Patch } from "../index.js";
import { tryParseBinding } from "../core/binding.js";
import { isCall, parseExpr } from "../core/expr.js";
import { ParseError, type ParsedLine, spanForLine } from "../core/text.js";
import { edgesFromChain, parsePinChain, parsePinRef } from "./pins.js";

export type ResolveGraphTarget = (name: string, line: ParsedLine) => GraphTarget;

export function parseGraphPatch(
  lines: ParsedLine[],
  patchIndex: number,
  resolveGraphTarget: ResolveGraphTarget,
): Patch {
  const patchLine = lines[patchIndex];
  const match = /^patch\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+(dry)\s+(run))?$/.exec(patchLine.text);
  if (!match) {
    throw new ParseError("invalid_patch", "Expected patch <graph>.", spanForLine(patchLine));
  }

  const patch: Patch = {
    kind: "patch",
    target: resolveGraphTarget(match[1], patchLine),
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

    patch.ops.push(...parseGraphOps(line));
  }

  return patch;
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

function parseGraphOps(line: ParsedLine): Op[] {
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
