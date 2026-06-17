import type {
  Binding,
  Call,
  Condition,
  Edge,
  Expr,
  Find,
  Graph,
  LglObject,
  Node,
  Op,
  Palette,
  Patch,
  Pin,
  PinChain,
  PinChainSegment,
  PinRef,
  Query,
  Target,
  Value,
} from "./index.js";

export function formatLglObject(object: LglObject): string {
  switch (object.kind) {
    case "graph":
      return formatGraph(object);
    case "query":
      return formatQuery(object);
    case "patch":
      return formatPatch(object);
    case "palette":
      return formatPalette(object);
    default:
      return assertNever(object);
  }
}

function formatGraph(graph: Graph): string {
  const lines = [formatHeader("graph", graph.target), ""];
  lines.push(...graph.nodes.map(formatNode));
  if (graph.pins && graph.pins.length > 0) {
    lines.push("", ...graph.pins.map(formatPin));
  }
  if (graph.edges.length > 0) {
    lines.push("", ...graph.edges.map(formatEdge));
  }
  return `${lines.join("\n")}\n`;
}

function formatQuery(query: Query): string {
  const lines = [formatHeader("query", query.target)];
  if (query.find) {
    lines.push("", formatFind(query.find));
  }
  return `${lines.join("\n")}\n`;
}

function formatPatch(patch: Patch): string {
  const lines = [formatHeader("patch", patch.target, patch.dryRun), ""];
  if (patch.bindings.length > 0) {
    lines.push(...patch.bindings.map(formatBinding), "");
  }
  lines.push(...patch.ops.map(formatOp));
  return `${trimTrailingBlankLines(lines).join("\n")}\n`;
}

function formatPalette(palette: Palette): string {
  const lines = [formatHeader("palette", palette.target), ""];
  for (const entry of palette.entries) {
    lines.push(
      `${entry.name} = palette(${formatObjectValue({
        id: entry.entry.id,
        ...(entry.meta ?? {}),
      })})`,
    );
  }
  return `${lines.join("\n")}\n`;
}

function formatHeader(kind: LglObject["kind"], target: Target, dryRun = false): string {
  const graph =
    target.graph.kind === "id"
      ? `id(${JSON.stringify(target.graph.id)})`
      : target.graph.name;
  return `${kind} ${target.domain}(${JSON.stringify(target.asset)}/${graph})${dryRun ? " dry run" : ""}`;
}

function formatNode(node: Node): string {
  const id = node.id ? `@${node.id}` : "";
  const args = Object.keys(node.fields).length > 0 ? formatObjectValue(node.fields) : "";
  const layout = node.layout ? ` ${formatObjectValue(node.layout as Record<string, Value>)}` : "";
  return `${node.alias}${id}: ${node.type}(${args})${layout}`;
}

function formatPin(pin: Pin): string {
  const meta: string[] = [];
  if (pin.value !== undefined) {
    meta.push(formatValue(pin.value));
  }
  if (pin.layout?.anchor) {
    meta.push(`anchor: ${formatValue(pin.layout.anchor)}`);
  }
  return `${pin.node}.${pin.name}: ${pin.type} ${pin.direction}${meta.length > 0 ? ` {${meta.join(", ")}}` : ""}`;
}

function formatEdge(edge: Edge): string {
  return `${formatPinRef(edge.from)} -> ${formatPinRef(edge.to)}`;
}

function formatFind(find: Find): string {
  switch (find.kind) {
    case "node":
      return `find node ${find.node}${find.with ? ` with ${find.with.join(", ")}` : ""}`;
    case "nodes":
      return `find nodes${find.where ? ` where ${formatCondition(find.where)}` : ""}${find.with ? ` with ${find.with.join(", ")}` : ""}`;
    case "path":
      return `find path from ${formatPinRef(find.from)}`;
    case "surrounding":
      return `find surrounding around ${find.around} depth ${find.depth}`;
    case "palette_entry":
      return `find palette entry${find.text ? ` ${JSON.stringify(find.text)}` : ""}${find.where ? ` where ${formatCondition(find.where)}` : ""}`;
    default:
      return assertNever(find);
  }
}

function formatCondition(condition: Condition): string {
  switch (condition.kind) {
    case "eq":
      return `${condition.field} = ${formatValue(condition.value)}`;
    case "contains":
      return `${condition.field} contains ${formatValue(condition.value)}`;
    case "and":
      return condition.conditions.map(formatCondition).join(" and ");
    default:
      return assertNever(condition);
  }
}

function formatBinding(binding: Binding): string {
  return `${binding.name} = ${formatExpr(binding.value)}`;
}

function formatOp(op: Op): string {
  switch (op.kind) {
    case "set":
      return `set ${op.target.node}.${op.target.field} = ${formatExpr(op.value)}`;
    case "add":
      return op.connect ? `add ${formatEdgeWithNode(op.connect, op.node)}` : `add ${op.node}`;
    case "insert":
      return `insert ${formatPinChain(op.chain)}`;
    case "connect":
      return `connect ${formatPinChain(op.chain)}`;
    case "disconnect":
      return "edge" in op
        ? `disconnect ${formatEdge(op.edge)}`
        : `disconnect ${formatPinRef(op.pin)}`;
    case "remove":
      return `remove ${op.node}`;
    case "move":
      return op.mode === "to"
        ? `move ${op.node} to (${op.at[0]}, ${op.at[1]})`
        : `move ${op.node} by (${op.delta[0]}, ${op.delta[1]})`;
    case "reconstruct":
      return `reconstruct ${op.node}${op.preserveLinks ? " preserve links" : ""}`;
    default:
      return assertNever(op);
  }
}

function formatEdgeWithNode(edge: Edge, node: string): string {
  if (edge.to.node === node) {
    return `${formatPinRef(edge.from)} -> ${formatPinRef(edge.to)}`;
  }
  return `${formatPinRef(edge.from)} -> ${formatPinRef(edge.to)}`;
}

function formatPinChain(chain: PinChain): string {
  return chain.segments.map(formatPinChainSegment).join(" -> ");
}

function formatPinChainSegment(segment: PinChainSegment): string {
  if (segment.kind === "pin") {
    return formatPinRef(segment.pin);
  }
  return `${formatPinRef(segment.input)}/${segment.output.pin}`;
}

function formatPinRef(pin: PinRef): string {
  return `${pin.node}.${pin.pin}`;
}

function formatExpr(expr: Expr): string {
  return isCall(expr) ? `${expr.callee}(${formatObjectValue(expr.args)})` : formatValue(expr);
}

function formatValue(value: Value): string {
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
  return formatObjectValue(value);
}

function formatObjectValue(value: Record<string, Value>): string {
  const entries = Object.entries(value).map(([key, item]) => `${key}: ${formatValue(item)}`);
  return `{${entries.join(", ")}}`;
}

function trimTrailingBlankLines(lines: string[]): string[] {
  const result = [...lines];
  while (result.length > 0 && result[result.length - 1] === "") {
    result.pop();
  }
  return result;
}

function isCall(value: Expr): value is Call {
  return typeof value === "object" && value !== null && !Array.isArray(value) && value.kind === "call";
}

function isName(value: Value): value is { kind: "name"; name: string } {
  return typeof value === "object" && value !== null && !Array.isArray(value) && value.kind === "name";
}

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}
