import type {
  Binding,
  Call,
  Condition,
  CreationEntry,
  Edge,
  Expr,
  Find,
  Graph,
  LglObject,
  Node,
  Op,
  Patch,
  Pin,
  PinRef,
  Query,
  Ref,
  Target,
  Value,
} from "./index.js";

const DEFAULT_ASSET_ALIAS = "asset";
const DEFAULT_GRAPH_ALIAS = "g";

export function formatLglObject(object: LglObject): string {
  switch (object.kind) {
    case "graph":
      return formatGraph(object);
    case "query":
      return formatQuery(object);
    case "patch":
      return formatPatch(object);
    case "creation_result":
      return formatCreationResult(object.target, object.entries);
    default:
      return assertNever(object);
  }
}

function formatGraph(graph: Graph): string {
  const lines = [...formatTargetBindings(graph.target), ""];
  lines.push(...graph.nodes.map((node) => formatNode(node, DEFAULT_GRAPH_ALIAS)));
  if (graph.pins && graph.pins.length > 0) {
    lines.push("", ...graph.pins.map(formatPin));
  }
  if (graph.edges.length > 0) {
    lines.push("", ...formatEdgeChainLines(graph.edges));
  }
  return `${trimTrailingBlankLines(lines).join("\n")}\n`;
}

function formatQuery(query: Query): string {
  const lines = [...formatTargetBindings(query.target), "", `query ${DEFAULT_GRAPH_ALIAS}`];
  if (query.find) {
    lines.push(formatFind(query.find));
  }
  if (query.where) {
    lines.push(`where ${formatCondition(query.where)}`);
  }
  if (query.with && query.with.length > 0) {
    lines.push(`with ${query.with.join(", ")}`);
  }
  if (query.orderBy && query.orderBy.length > 0) {
    lines.push(`order by ${query.orderBy.map((item) => `${item.key} ${item.direction}`).join(", ")}`);
  }
  if (query.page?.limit !== undefined) {
    lines.push(`page limit ${query.page.limit}`);
  }
  if (query.page?.after !== undefined) {
    lines.push(`page after ${JSON.stringify(query.page.after)}`);
  }
  return `${lines.join("\n")}\n`;
}

function formatPatch(patch: Patch): string {
  const lines = [
    ...formatTargetBindings(patch.target),
    "",
    `patch ${DEFAULT_GRAPH_ALIAS}${patch.dryRun ? " dry run" : ""}`,
  ];
  if (patch.bindings.length > 0) {
    lines.push(...patch.bindings.map(formatBinding));
  }
  lines.push(...patch.ops.map(formatOp));
  return `${trimTrailingBlankLines(lines).join("\n")}\n`;
}

function formatCreationResult(target: Target, entries: CreationEntry[]): string {
  const lines = [...formatTargetBindings(target), ""];
  for (const entry of entries) {
    if ("palette" in entry) {
      lines.push(
        `${entry.name} = palette(${formatArgList({
          id: entry.palette.id,
          ...(entry.label ? { label: entry.label } : {}),
          ...(entry.category ? { category: entry.category } : {}),
        })})`,
      );
    } else {
      lines.push(`${entry.name} = ${formatCall(entry.constructor)}`);
    }
    if (entry.pins) {
      lines.push(...entry.pins.map((pin) => formatPin({ ...pin, node: entry.name })));
    }
  }
  return `${lines.join("\n")}\n`;
}

function formatTargetBindings(target: Target): string[] {
  return [
    `${DEFAULT_ASSET_ALIAS} = asset(path: ${JSON.stringify(target.asset)}, type: ${target.domain})`,
    `${DEFAULT_GRAPH_ALIAS} = graph(domain: ${target.domain}, asset: ${DEFAULT_ASSET_ALIAS}, graph: ${formatGraphRef(target)})`,
  ];
}

function formatGraphRef(target: Target): string {
  return target.graph.kind === "id"
    ? `id(id: ${JSON.stringify(target.graph.id)})`
    : target.graph.name;
}

function formatNode(node: Node, graphAlias: string): string {
  const args: Record<string, Expr> = {
    graph: localRef(graphAlias),
    type: nameValue(node.type),
    ...(node.id ? { id: node.id } : {}),
    ...node.fields,
    ...(node.at ? { at: node.at } : {}),
    ...(node.size ? { size: node.size } : {}),
  };
  return `${node.alias} = node(${formatArgList(args)})`;
}

function formatPin(pin: Pin): string {
  const args: Record<string, Expr> = {
    type: nameValue(pin.type),
    direction: nameValue(pin.direction),
    ...(pin.value !== undefined ? { value: pin.value } : {}),
    ...(pin.anchor ? { anchor: pin.anchor } : {}),
  };
  return `${pin.node}.${pin.name} = pin(${formatArgList(args)})`;
}

function formatEdgeChainLines(edges: Edge[]): string[] {
  return edges.map((edge) => `${formatPinRef(edge.from)} -> ${formatPinRef(edge.to)}`);
}

function formatFind(find: Find): string {
  switch (find.kind) {
    case "nodes":
      return `find nodes${find.text ? ` ${JSON.stringify(find.text)}` : ""}`;
    case "path":
      return `find path ${find.direction} ${formatPinRef(find.pin)}`;
    case "palette_entry":
      return `find palette entry${find.text ? ` ${JSON.stringify(find.text)}` : ""}${find.pinContext ? ` ${find.pinContext.direction} ${formatPinRef(find.pinContext.pin)}` : ""}`;
    default:
      return assertNever(find);
  }
}

function formatCondition(condition: Condition): string {
  switch (condition.kind) {
    case "eq":
      return `${formatFieldPath(condition.field.path)} = ${formatExpr(condition.value)}`;
    case "ne":
      return `${formatFieldPath(condition.field.path)} != ${formatExpr(condition.value)}`;
    case "contains":
      return `${formatFieldPath(condition.field.path)} ~= ${formatExpr(condition.value)}`;
    case "compare":
      return `${formatFieldPath(condition.field.path)} ${formatCompareOp(condition.op)} ${formatExpr(condition.value)}`;
    case "not":
      return `not ${formatConditionAtom(condition.condition)}`;
    case "and":
      return condition.conditions.map(formatConditionAtom).join(" and ");
    case "or":
      return condition.conditions.map(formatConditionAtom).join(" or ");
    default:
      return assertNever(condition);
  }
}

function formatConditionAtom(condition: Condition): string {
  return condition.kind === "and" || condition.kind === "or"
    ? `(${formatCondition(condition)})`
    : formatCondition(condition);
}

function formatCompareOp(op: "gt" | "gte" | "lt" | "lte"): string {
  switch (op) {
    case "gt":
      return ">";
    case "gte":
      return ">=";
    case "lt":
      return "<";
    case "lte":
      return "<=";
    default:
      return assertNever(op);
  }
}

function formatFieldPath(path: string[]): string {
  return path.join(".");
}

function formatBinding(binding: Binding): string {
  return `${formatBindingTarget(binding.target)} = ${formatExpr(binding.value)}`;
}

function formatBindingTarget(target: Binding["target"]): string {
  switch (target.kind) {
    case "local":
      return target.name;
    case "member":
      return `${target.object}.${target.member}`;
    default:
      return assertNever(target);
  }
}

function formatOp(op: Op): string {
  switch (op.kind) {
    case "set":
      return `set ${op.target.object}.${op.target.field} = ${formatExpr(op.value)}`;
    case "add":
      return op.connect
        ? `add ${op.binding} ${formatPinRef(op.connect.from)} -> ${formatPinRef(op.connect.to)}`
        : `add ${op.binding}`;
    case "insert":
      return `insert ${formatPinRef(op.from)} -> ${formatPinRef(op.input)}/${op.output.pin} -> ${formatPinRef(op.to)}`;
    case "connect":
      return `connect ${formatPinRef(op.edge.from)} -> ${formatPinRef(op.edge.to)}`;
    case "disconnect":
      return "edge" in op
        ? `disconnect ${formatPinRef(op.edge.from)} -> ${formatPinRef(op.edge.to)}`
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

function formatPinRef(pin: PinRef): string {
  return `${pin.node}.${pin.pin}`;
}

function formatExpr(expr: Expr): string {
  if (isCall(expr)) {
    return formatCall(expr);
  }
  if (isRef(expr)) {
    return formatRef(expr);
  }
  return formatValue(expr);
}

function formatCall(call: Call): string {
  return `${call.callee}(${formatArgList(call.args)})`;
}

function formatArgList(args: Record<string, Expr>): string {
  return Object.entries(args)
    .map(([key, value]) => `${key}: ${formatExpr(value)}`)
    .join(", ");
}

function formatRef(ref: Ref): string {
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
  return `{${Object.entries(value).map(([key, item]) => `${key}: ${formatValue(item)}`).join(", ")}}`;
}

function localRef(name: string): Ref {
  return { kind: "local", name };
}

function nameValue(name: string): Value {
  return { kind: "name", name };
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

function isRef(value: Expr): value is Ref {
  return (
    typeof value === "object" &&
    value !== null &&
    !Array.isArray(value) &&
    (value.kind === "local" || value.kind === "member" || value.kind === "id")
  );
}

function isName(value: Value): value is { kind: "name"; name: string } {
  return typeof value === "object" && value !== null && !Array.isArray(value) && value.kind === "name";
}

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}
