import type {
  Binding,
  BindingValue,
  CreationEntry,
  Edge,
  Expr,
  Find,
  Graph,
  LglObject,
  Node,
  NodeCreation,
  Op,
  Patch,
  Pin,
  PinRef,
  Query,
  Target,
} from "../index.js";
import { formatBindingTarget } from "../core/binding.js";
import { formatCondition } from "../core/condition.js";
import { formatArgList, formatCall, formatExpr, localRef, nameValue } from "../core/expr.js";

const DEFAULT_ASSET_ALIAS = "asset";
const DEFAULT_GRAPH_ALIAS = "g";

export function formatGraphLglObject(object: LglObject): string {
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
      lines.push(`${entry.name} = node(${formatArgList({ palette: entry.palette.id, ...(entry.defaults ?? {}) })})`);
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

function formatBinding(binding: Binding): string {
  return `${formatBindingTarget(binding.target)} = ${formatBindingValue(binding.value)}`;
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

function formatBindingValue(value: BindingValue): string {
  if (isNodeCreation(value)) {
    return formatNodeCreation(value);
  }
  return formatExpr(value);
}

function formatNodeCreation(creation: NodeCreation): string {
  switch (creation.kind) {
    case "palette_node":
      return `node(${formatArgList({
        palette: creation.palette,
        ...(creation.defaults ?? {}),
      })})`;
    case "shortcut_node":
      return formatCall(creation.constructor);
    default:
      return assertNever(creation);
  }
}

function formatPinRef(pin: PinRef): string {
  return `${pin.node}.${pin.pin}`;
}

function trimTrailingBlankLines(lines: string[]): string[] {
  const result = [...lines];
  while (result.length > 0 && result[result.length - 1] === "") {
    result.pop();
  }
  return result;
}

function isNodeCreation(value: BindingValue): value is NodeCreation {
  return (
    typeof value === "object" &&
    value !== null &&
    !Array.isArray(value) &&
    (value.kind === "palette_node" || value.kind === "shortcut_node")
  );
}

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}
