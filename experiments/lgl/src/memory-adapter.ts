import type {
  Adapter,
  Binding,
  Call,
  Condition,
  Edge,
  Graph,
  Node,
  ObjectResult,
  Op,
  Patch,
  Pin,
  PinChain,
  PinRef,
  Query,
  Target,
  Value,
} from "./index.js";

export interface CreateMemoryGraphAdapterOptions {
  domain: string;
  graphs: Graph[];
}

export interface MemoryGraphAdapter extends Adapter {
  getGraph(target: Target): Graph | undefined;
}

export function createMemoryGraphAdapter(
  options: CreateMemoryGraphAdapterOptions,
): MemoryGraphAdapter {
  const graphs = new Map<string, Graph>();
  for (const graph of options.graphs) {
    graphs.set(targetKey(graph.target), cloneGraph(graph));
  }

  function resolveGraph(target: Target): Graph | undefined {
    return graphs.get(targetKey(target));
  }

  return {
    domain: options.domain,
    getGraph(target) {
      const graph = resolveGraph(target);
      return graph ? cloneGraph(graph) : undefined;
    },
    async query(query) {
      const graph = resolveGraph(query.target);
      if (!graph) {
        return graphNotFound(query.target);
      }
      return { object: executeQuery(graph, query), diagnostics: [] };
    },
    async patch(patch) {
      const graph = resolveGraph(patch.target);
      if (!graph) {
        return graphNotFound(patch.target);
      }

      const working = cloneGraph(graph);
      const result = executePatch(working, patch);
      if (result.diagnostics.length > 0) {
        return result;
      }

      if (!patch.dryRun) {
        graphs.set(targetKey(patch.target), cloneGraph(working));
      }

      return { object: working, diagnostics: [] };
    },
  };
}

function executeQuery(graph: Graph, query: Query): Graph {
  const find = query.find;
  if (!find) {
    return cloneGraph(graph);
  }

  switch (find.kind) {
    case "node":
      return graphSnippet(graph, [find.node], true);
    case "nodes": {
      const aliases = graph.nodes
        .filter((node) => matchesCondition(node, graph, find.where))
        .map((node) => node.alias);
      return graphSnippet(graph, aliases, find.with?.includes("pins") ?? false);
    }
    case "path": {
      const aliases = walkPathAliases(graph, find.from);
      return graphSnippet(graph, aliases, false);
    }
    case "surrounding": {
      const aliases = surroundingAliases(graph, find.around, find.depth);
      return graphSnippet(graph, aliases, true);
    }
    case "palette_entry":
      return cloneGraph(graph);
    default:
      return assertNever(find);
  }
}

function executePatch(graph: Graph, patch: Patch): ObjectResult {
  const bindingMap = new Map<string, Binding>();
  for (const binding of patch.bindings) {
    bindingMap.set(binding.name, binding);
  }

  for (const op of patch.ops) {
    const diagnostic = applyOp(graph, bindingMap, op);
    if (diagnostic) {
      return { diagnostics: [diagnostic] };
    }
  }

  return { object: graph, diagnostics: [] };
}

function applyOp(graph: Graph, bindings: Map<string, Binding>, op: Op): ObjectResult["diagnostics"][number] | undefined {
  switch (op.kind) {
    case "insert": {
      const first = firstPin(op.chain);
      const last = lastPin(op.chain);
      if (!first || !last) {
        return diagnostic("invalid_chain", "Insert requires first and last pins.");
      }
      const existingIndex = graph.edges.findIndex((edge) => sameEdge(edge, { from: first, to: last }));
      if (existingIndex < 0) {
        return diagnostic("missing_insert_edge", "Insert requires an existing direct edge.");
      }
      const node = nodeFromBinding(op.node, bindings);
      if (!node) {
        return diagnostic("unknown_node_binding", `No node binding exists for ${op.node}.`);
      }
      graph.nodes.push(node);
      graph.edges.splice(existingIndex, 1);
      graph.edges.push(...edgesFromChain(op.chain));
      return undefined;
    }
    case "connect":
      graph.edges.push(...edgesFromChain(op.chain).filter((edge) => !hasEdge(graph, edge)));
      return undefined;
    case "disconnect":
      if ("edge" in op) {
        const before = graph.edges.length;
        graph.edges = graph.edges.filter((edge) => !sameEdge(edge, op.edge));
        return graph.edges.length === before
          ? diagnostic("missing_edge", "Disconnect edge does not exist.")
          : undefined;
      }
      graph.edges = graph.edges.filter(
        (edge) => !samePin(edge.from, op.pin) && !samePin(edge.to, op.pin),
      );
      return undefined;
    case "move": {
      const node = graph.nodes.find((candidate) => candidate.alias === op.node);
      if (!node) {
        return diagnostic("unknown_node", `Node ${op.node} does not exist.`);
      }
      const current = node.layout?.at ?? [0, 0];
      node.layout = {
        ...(node.layout ?? {}),
        at: op.mode === "to" ? op.at : [current[0] + op.delta[0], current[1] + op.delta[1]],
      };
      return undefined;
    }
    case "remove":
      graph.nodes = graph.nodes.filter((node) => node.alias !== op.node);
      graph.pins = graph.pins?.filter((pin) => pin.node !== op.node);
      graph.edges = graph.edges.filter((edge) => edge.from.node !== op.node && edge.to.node !== op.node);
      return undefined;
    case "set": {
      const node = graph.nodes.find((candidate) => candidate.alias === op.target.node);
      if (!node) {
        return diagnostic("unknown_node", `Node ${op.target.node} does not exist.`);
      }
      node.fields[op.target.field] = exprToValue(op.value);
      return undefined;
    }
    case "add": {
      const node = nodeFromBinding(op.node, bindings);
      if (!node) {
        return diagnostic("unknown_node_binding", `No node binding exists for ${op.node}.`);
      }
      graph.nodes.push(node);
      if (op.connect) {
        graph.edges.push(op.connect);
      }
      return undefined;
    }
    case "reconstruct":
      return undefined;
    default:
      return assertNever(op);
  }
}

function graphSnippet(graph: Graph, aliases: string[], includePins: boolean): Graph {
  const aliasSet = new Set(aliases);
  return {
    kind: "graph",
    target: graph.target,
    nodes: graph.nodes.filter((node) => aliasSet.has(node.alias)).map(cloneNode),
    edges: graph.edges.filter((edge) => aliasSet.has(edge.from.node) && aliasSet.has(edge.to.node)).map(cloneEdge),
    ...(includePins && graph.pins
      ? { pins: graph.pins.filter((pin) => aliasSet.has(pin.node)).map(clonePin) }
      : {}),
  };
}

function matchesCondition(node: Node, graph: Graph, condition: Condition | undefined): boolean {
  if (!condition) {
    return true;
  }

  switch (condition.kind) {
    case "eq":
      return String(readConditionField(node, graph, condition.field)) === valueToConditionString(condition.value);
    case "contains":
      return String(readConditionField(node, graph, condition.field)).includes(valueToConditionString(condition.value));
    case "and":
      return condition.conditions.every((item) => matchesCondition(node, graph, item));
    default:
      return assertNever(condition);
  }
}

function readConditionField(node: Node, graph: Graph, field: string): unknown {
  if (field === "type") {
    return node.type;
  }
  if (field === "alias") {
    return node.alias;
  }
  if (field === "id") {
    return node.id;
  }
  if (field.startsWith("pin ")) {
    const pinName = field.slice("pin ".length).trim();
    return graph.pins?.some((pin) => pin.node === node.alias && pin.name === pinName) ?? false;
  }
  return node.fields[field];
}

function valueToConditionString(value: Value): string {
  if (isName(value)) {
    return value.name;
  }
  return String(value);
}

function walkPathAliases(graph: Graph, from: PinRef): string[] {
  const aliases: string[] = [from.node];
  const seen = new Set<string>([from.node]);
  let current = from;

  for (;;) {
    const next = graph.edges.find((edge) => samePin(edge.from, current));
    if (!next || seen.has(next.to.node)) {
      break;
    }
    aliases.push(next.to.node);
    seen.add(next.to.node);
    current = next.to;
  }

  return aliases;
}

function surroundingAliases(graph: Graph, around: string, depth: number): string[] {
  const aliases = new Set<string>([around]);
  let frontier = new Set<string>([around]);

  for (let step = 0; step < depth; step += 1) {
    const next = new Set<string>();
    for (const edge of graph.edges) {
      if (frontier.has(edge.from.node)) {
        next.add(edge.to.node);
      }
      if (frontier.has(edge.to.node)) {
        next.add(edge.from.node);
      }
    }
    for (const alias of next) {
      aliases.add(alias);
    }
    frontier = next;
  }

  return [...aliases];
}

function nodeFromBinding(alias: string, bindings: Map<string, Binding>): Node | undefined {
  const binding = bindings.get(alias);
  if (!binding || !isCall(binding.value)) {
    return undefined;
  }
  return {
    alias,
    type: binding.value.callee,
    fields: cloneValueRecord(binding.value.args),
  };
}

function exprToValue(value: Binding["value"]): Value {
  if (isCall(value)) {
    return { callee: value.callee, args: cloneValueRecord(value.args) };
  }
  return cloneValue(value);
}

function firstPin(chain: PinChain): PinRef | undefined {
  const segment = chain.segments[0];
  return segment?.kind === "pin" ? segment.pin : segment?.input;
}

function lastPin(chain: PinChain): PinRef | undefined {
  const segment = chain.segments[chain.segments.length - 1];
  return segment?.kind === "pin" ? segment.pin : segment?.output;
}

function edgesFromChain(chain: PinChain): Edge[] {
  const pins: PinRef[] = [];
  for (const segment of chain.segments) {
    if (segment.kind === "pin") {
      pins.push(segment.pin);
    } else {
      pins.push(segment.input, segment.output);
    }
  }

  const edges: Edge[] = [];
  for (let index = 0; index < pins.length - 1; index += 2) {
    edges.push({ from: pins[index], to: pins[index + 1] });
  }
  return edges;
}

function hasEdge(graph: Graph, edge: Edge): boolean {
  return graph.edges.some((candidate) => sameEdge(candidate, edge));
}

function sameEdge(left: Edge, right: Edge): boolean {
  return samePin(left.from, right.from) && samePin(left.to, right.to);
}

function samePin(left: PinRef, right: PinRef): boolean {
  return left.node === right.node && left.pin === right.pin;
}

function targetKey(target: Target): string {
  const graph = target.graph.kind === "id" ? `id:${target.graph.id}` : `name:${target.graph.name}`;
  return `${target.domain}:${target.asset}:${graph}`;
}

function graphNotFound(target: Target): ObjectResult {
  return {
    diagnostics: [
      diagnostic(
        "graph_not_found",
        `No in-memory graph is registered for ${target.domain} ${target.asset}.`,
      ),
    ],
  };
}

function diagnostic(code: string, message: string): ObjectResult["diagnostics"][number] {
  return { severity: "error", code, message };
}

function isCall(value: Binding["value"]): value is Call {
  return typeof value === "object" && value !== null && !Array.isArray(value) && value.kind === "call";
}

function isName(value: Value): value is { kind: "name"; name: string } {
  return typeof value === "object" && value !== null && !Array.isArray(value) && value.kind === "name";
}

function cloneGraph(graph: Graph): Graph {
  return {
    kind: "graph",
    target: structuredClone(graph.target),
    nodes: graph.nodes.map(cloneNode),
    edges: graph.edges.map(cloneEdge),
    ...(graph.pins ? { pins: graph.pins.map(clonePin) } : {}),
  };
}

function cloneNode(node: Node): Node {
  return structuredClone(node);
}

function cloneEdge(edge: Edge): Edge {
  return structuredClone(edge);
}

function clonePin(pin: Pin): Pin {
  return structuredClone(pin);
}

function cloneValue(value: Value): Value {
  return structuredClone(value);
}

function cloneValueRecord(value: Record<string, Value>): Record<string, Value> {
  return structuredClone(value);
}

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}
