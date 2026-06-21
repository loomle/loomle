import type {
  Adapter,
  Binding,
  Call,
  Condition,
  CreationEntry,
  Edge,
  Expr,
  FindPaletteEntry,
  Graph,
  Node,
  NodeCreation,
  ObjectResult,
  Op,
  Patch,
  Pin,
  PinRef,
  Query,
  Ref,
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

interface PatchPlanResult extends ObjectResult {
  object?: Graph;
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
      return executeQuery(graph, query);
    },
    async patch(patch) {
      const graph = resolveGraph(patch.target);
      if (!graph) {
        return graphNotFound(patch.target);
      }

      const result = planPatch(graph, patch);
      if (result.diagnostics.length > 0) {
        return result;
      }

      if (!result.object) {
        return { diagnostics: [diagnostic("patch_plan_failed", "Patch planning did not produce a graph.")] };
      }

      if (!patch.dryRun) {
        graphs.set(targetKey(patch.target), cloneGraph(result.object));
      }

      return result;
    },
  };
}

function executeQuery(graph: Graph, query: Query): ObjectResult {
  const find = query.find;
  if (!find) {
    return { object: cloneGraph(graph), diagnostics: [] };
  }

  switch (find.kind) {
    case "nodes": {
      const nodes = graph.nodes
        .filter((node) => matchesText(node, find.text))
        .filter((node) => matchesCondition(node, graph, query.where));
      const aliases = pageItems(
        sortItems(nodes, query, (node, key) => readConditionField(node, graph, key.split("."))),
        query,
      ).map((node) => node.alias);
      return { object: graphSnippet(graph, aliases, query.with?.includes("pins") ?? false), diagnostics: [] };
    }
    case "path": {
      const aliases = find.direction === "from"
        ? walkPathAliasesFrom(graph, find.pin)
        : walkPathAliasesTo(graph, find.pin);
      return { object: graphSnippet(graph, aliases, query.with?.includes("pins") ?? false), diagnostics: [] };
    }
    case "palette_entry":
      return {
        object: {
          kind: "creation_result",
          target: graph.target,
          entries: findPaletteEntries(find, query),
        },
        diagnostics: [],
      };
    default:
      return assertNever(find);
  }
}

function findPaletteEntries(find: FindPaletteEntry, query: Query): CreationEntry[] {
  const includePins = query.with?.includes("pins") ?? false;
  const includeDefaults = query.with?.includes("defaults") ?? false;
  const entries = memoryPaletteEntries(includePins, includeDefaults)
    .filter((entry) => matchesPaletteText(entry, find.text))
    .filter((entry) => matchesPaletteCondition(entry, query.where));
  return pageItems(
    sortItems(entries, query, (entry, key) => readPaletteField(entry, key.split("."))),
    query,
  );
}

function sortItems<T>(items: T[], query: Query, readField: (item: T, key: string) => unknown): T[] {
  if (!query.orderBy || query.orderBy.length === 0) {
    return items;
  }
  return [...items].sort((left, right) => {
    for (const order of query.orderBy ?? []) {
      const result = compareSortable(readField(left, order.key), readField(right, order.key));
      if (result !== 0) {
        return order.direction === "desc" ? -result : result;
      }
    }
    return 0;
  });
}

function pageItems<T>(items: T[], query: Query): T[] {
  const start = query.page?.after ? cursorOffset(query.page.after) : 0;
  const limit = query.page?.limit ?? 50;
  return items.slice(start, start + limit);
}

function cursorOffset(cursor: string): number {
  const match = /^offset:(\d+)$/.exec(cursor);
  return match ? Number(match[1]) : 0;
}

function compareSortable(left: unknown, right: unknown): number {
  if (typeof left === "number" && typeof right === "number") {
    return left - right;
  }
  return String(left ?? "").localeCompare(String(right ?? ""));
}

function memoryPaletteEntries(includePins: boolean, includeDefaults: boolean): CreationEntry[] {
  return [
    {
      name: "Delay",
      constructor: call("delay", { duration: 1.0 }),
      ...(includeDefaults ? { defaults: { duration: 1.0 } } : {}),
      ...(includePins
        ? {
            pins: [
              pin("Delay", "Exec", "exec", "in"),
              pin("Delay", "Duration", "float", "in", 1.0),
              pin("Delay", "Completed", "exec", "out"),
            ],
          }
        : {}),
    },
    {
      name: "Branch",
      constructor: call("branch"),
      ...(includePins
        ? {
            pins: [
              pin("Branch", "Exec", "exec", "in"),
              pin("Branch", "Condition", "boolean", "in"),
              pin("Branch", "Then", "exec", "out"),
              pin("Branch", "Else", "exec", "out"),
            ],
          }
        : {}),
    },
    {
      name: "PrintString",
      palette: { kind: "palette", id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString" },
      label: "Print String",
      category: "Utilities/String",
      ...(includeDefaults ? { defaults: { InString: "" } } : {}),
      ...(includePins
        ? {
            pins: [
              pin("PrintString", "Exec", "exec", "in"),
              pin("PrintString", "InString", "string", "in", includeDefaults ? "" : undefined),
              pin("PrintString", "Then", "exec", "out"),
            ],
          }
        : {}),
    },
  ];
}

function call(callee: string, args: Record<string, Expr> = {}): Call {
  return { kind: "call", callee, args };
}

function pin(node: string, name: string, type: string, direction: "in" | "out", value?: Expr): Pin {
  return {
    node,
    name,
    type,
    direction,
    ...(value !== undefined ? { value } : {}),
  };
}

function matchesPaletteText(entry: CreationEntry, text: string | undefined): boolean {
  if (!text) {
    return true;
  }
  const lowered = text.toLowerCase();
  return paletteSearchFields(entry).some((field) => field.toLowerCase().includes(lowered));
}

function paletteSearchFields(entry: CreationEntry): string[] {
  if ("palette" in entry) {
    return [entry.name, entry.label ?? "", entry.category ?? "", entry.palette.id];
  }
  return [entry.name, entry.constructor.callee];
}

function matchesPaletteCondition(entry: CreationEntry, condition: Condition | undefined): boolean {
  if (!condition) {
    return true;
  }

  switch (condition.kind) {
    case "eq":
      return String(readPaletteField(entry, condition.field.path)) === exprToConditionString(condition.value);
    case "ne":
      return String(readPaletteField(entry, condition.field.path)) !== exprToConditionString(condition.value);
    case "contains":
      return String(readPaletteField(entry, condition.field.path)).includes(exprToConditionString(condition.value));
    case "compare":
      return compareValues(readPaletteField(entry, condition.field.path), condition.op, condition.value);
    case "not":
      return !matchesPaletteCondition(entry, condition.condition);
    case "and":
      return condition.conditions.every((item) => matchesPaletteCondition(entry, item));
    case "or":
      return condition.conditions.some((item) => matchesPaletteCondition(entry, item));
    default:
      return assertNever(condition);
  }
}

function readPaletteField(entry: CreationEntry, path: string[]): unknown {
  const field = path.join(".");
  if (field === "name") {
    return entry.name;
  }
  if ("palette" in entry) {
    if (field === "palette" || field === "id") {
      return entry.palette.id;
    }
    if (field === "label") {
      return entry.label;
    }
    if (field === "category") {
      return entry.category;
    }
  }
  if (!("palette" in entry) && (field === "constructor" || field === "type")) {
    return entry.constructor.callee;
  }
  return undefined;
}

function planPatch(graph: Graph, patch: Patch): PatchPlanResult {
  const working = cloneGraph(graph);
  const bindingMap = new Map<string, Binding>();
  for (const binding of patch.bindings) {
    if (binding.target.kind === "local") {
      bindingMap.set(binding.target.name, binding);
    }
  }

  const addedAliases = collectAddedAliases(patch.ops);
  for (const alias of addedAliases) {
    if (!nodeFromBinding(alias, bindingMap)) {
      return { diagnostics: [diagnostic("unknown_node_binding", `No node binding exists for ${alias}.`)] };
    }
  }

  for (const op of patch.ops) {
    const diagnostic = validateOpReferences(graph, bindingMap, addedAliases, op);
    if (diagnostic) {
      return { diagnostics: [diagnostic] };
    }
  }

  for (const op of patch.ops) {
    const diagnostic = applyOp(working, bindingMap, op);
    if (diagnostic) {
      return { diagnostics: [diagnostic] };
    }
  }

  return { object: working, diagnostics: [] };
}

function collectAddedAliases(ops: Op[]): Set<string> {
  const aliases = new Set<string>();
  for (const op of ops) {
    if (op.kind === "add") {
      aliases.add(op.binding);
    } else if (op.kind === "insert") {
      aliases.add(op.node);
    }
  }
  return aliases;
}

function validateOpReferences(
  graph: Graph,
  bindings: Map<string, Binding>,
  addedAliases: Set<string>,
  op: Op,
): ObjectResult["diagnostics"][number] | undefined {
  switch (op.kind) {
    case "insert":
      if (!hasEdge(graph, { from: op.from, to: op.to })) {
        return diagnostic("missing_insert_edge", "Insert requires an existing direct edge.");
      }
      return validateEdgeNodes(graph, addedAliases, { from: op.input, to: op.output });
    case "connect":
      return validateEdgeNodes(graph, addedAliases, op.edge);
    case "add":
      if (!nodeFromBinding(op.binding, bindings)) {
        return diagnostic("unknown_node_binding", `No node binding exists for ${op.binding}.`);
      }
      return op.connect ? validateEdgeNodes(graph, addedAliases, op.connect) : undefined;
    case "disconnect":
      if ("edge" in op && !hasEdge(graph, op.edge)) {
        return diagnostic("missing_edge", "Disconnect edge does not exist.");
      }
      return undefined;
    case "move":
      return graphHasNode(graph, op.node) || addedAliases.has(op.node)
        ? undefined
        : diagnostic("unknown_node", `Node ${op.node} does not exist.`);
    case "set":
      return graphHasNode(graph, op.target.object) || addedAliases.has(op.target.object)
        ? undefined
        : diagnostic("unknown_node", `Node ${op.target.object} does not exist.`);
    case "remove":
    case "reconstruct":
      return graphHasNode(graph, op.node) || addedAliases.has(op.node)
        ? undefined
        : diagnostic("unknown_node", `Node ${op.node} does not exist.`);
    default:
      return assertNever(op);
  }
}

function validateEdgeNodes(
  graph: Graph,
  addedAliases: Set<string>,
  edge: Edge,
): ObjectResult["diagnostics"][number] | undefined {
  for (const pin of [edge.from, edge.to]) {
    if (!graphHasNode(graph, pin.node) && !addedAliases.has(pin.node)) {
      return diagnostic("unknown_node", `Node ${pin.node} does not exist.`);
    }
  }
  return undefined;
}

function graphHasNode(graph: Graph, alias: string): boolean {
  return graph.nodes.some((node) => node.alias === alias);
}

function applyOp(
  graph: Graph,
  bindings: Map<string, Binding>,
  op: Op,
): ObjectResult["diagnostics"][number] | undefined {
  switch (op.kind) {
    case "insert": {
      const existingIndex = graph.edges.findIndex((edge) => sameEdge(edge, { from: op.from, to: op.to }));
      if (existingIndex < 0) {
        return diagnostic("missing_insert_edge", "Insert requires an existing direct edge.");
      }
      const node = nodeFromBinding(op.node, bindings);
      if (!node) {
        return diagnostic("unknown_node_binding", `No node binding exists for ${op.node}.`);
      }
      graph.nodes.push(node);
      graph.edges.splice(existingIndex, 1);
      graph.edges.push({ from: op.from, to: op.input }, { from: op.output, to: op.to });
      return undefined;
    }
    case "connect":
      if (!hasEdge(graph, op.edge)) {
        graph.edges.push(op.edge);
      }
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
      const current = node.at ?? [0, 0];
      node.at = op.mode === "to" ? op.at : [current[0] + op.delta[0], current[1] + op.delta[1]];
      return undefined;
    }
    case "remove":
      graph.nodes = graph.nodes.filter((node) => node.alias !== op.node);
      graph.pins = graph.pins?.filter((pin) => pin.node !== op.node);
      graph.edges = graph.edges.filter((edge) => edge.from.node !== op.node && edge.to.node !== op.node);
      return undefined;
    case "set": {
      const node = graph.nodes.find((candidate) => candidate.alias === op.target.object);
      if (!node) {
        return diagnostic("unknown_node", `Node ${op.target.object} does not exist.`);
      }
      node.fields[op.target.field] = op.value;
      return undefined;
    }
    case "add": {
      const node = nodeFromBinding(op.binding, bindings);
      if (!node) {
        return diagnostic("unknown_node_binding", `No node binding exists for ${op.binding}.`);
      }
      graph.nodes.push(node);
      if (op.connect && !hasEdge(graph, op.connect)) {
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

function matchesText(node: Node, text: string | undefined): boolean {
  if (!text) {
    return true;
  }
  const lowered = text.toLowerCase();
  return node.alias.toLowerCase().includes(lowered) || node.type.toLowerCase().includes(lowered);
}

function matchesCondition(node: Node, graph: Graph, condition: Condition | undefined): boolean {
  if (!condition) {
    return true;
  }

  switch (condition.kind) {
    case "eq":
      return String(readConditionField(node, graph, condition.field.path)) === exprToConditionString(condition.value);
    case "ne":
      return String(readConditionField(node, graph, condition.field.path)) !== exprToConditionString(condition.value);
    case "contains":
      return String(readConditionField(node, graph, condition.field.path)).includes(exprToConditionString(condition.value));
    case "compare":
      return compareValues(readConditionField(node, graph, condition.field.path), condition.op, condition.value);
    case "not":
      return !matchesCondition(node, graph, condition.condition);
    case "and":
      return condition.conditions.every((item) => matchesCondition(node, graph, item));
    case "or":
      return condition.conditions.some((item) => matchesCondition(node, graph, item));
    default:
      return assertNever(condition);
  }
}

function readConditionField(node: Node, graph: Graph, path: string[]): unknown {
  const field = path.join(".");
  if (field === "type") {
    return node.type;
  }
  if (field === "name" || field === "alias") {
    return node.alias;
  }
  if (field === "id") {
    return node.id;
  }
  if (path[0] === "pin" && path[1]) {
    return graph.pins?.some((pin) => pin.node === node.alias && pin.name === path[1]) ?? false;
  }
  return node.fields[path[0]];
}

function compareValues(left: unknown, op: "gt" | "gte" | "lt" | "lte", right: Expr): boolean {
  const leftNumber = typeof left === "number" ? left : Number(left);
  const rightNumber = Number(exprToConditionString(right));
  if (Number.isNaN(leftNumber) || Number.isNaN(rightNumber)) {
    return false;
  }
  switch (op) {
    case "gt":
      return leftNumber > rightNumber;
    case "gte":
      return leftNumber >= rightNumber;
    case "lt":
      return leftNumber < rightNumber;
    case "lte":
      return leftNumber <= rightNumber;
    default:
      return assertNever(op);
  }
}

function exprToConditionString(value: Expr): string {
  if (isName(value)) {
    return value.name;
  }
  if (isRef(value)) {
    return value.kind === "member" ? `${value.object}.${value.member}` : value.kind === "id" ? value.id : value.name;
  }
  return String(value);
}

function walkPathAliasesFrom(graph: Graph, from: PinRef): string[] {
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

function walkPathAliasesTo(graph: Graph, to: PinRef): string[] {
  const aliases: string[] = [to.node];
  const seen = new Set<string>([to.node]);
  let current = to;

  for (;;) {
    const previous = graph.edges.find((edge) => samePin(edge.to, current));
    if (!previous || seen.has(previous.from.node)) {
      break;
    }
    aliases.unshift(previous.from.node);
    seen.add(previous.from.node);
    current = previous.from;
  }

  return aliases;
}

function nodeFromBinding(alias: string, bindings: Map<string, Binding>): Node | undefined {
  const binding = bindings.get(alias);
  if (!binding) {
    return undefined;
  }

  if (isNodeCreation(binding.value)) {
    return nodeFromCreation(alias, binding.value);
  }

  if (!isCall(binding.value)) {
    return undefined;
  }

  if (binding.value.callee === "node") {
    const type = symbolName(binding.value.args.type);
    if (!type) {
      return undefined;
    }
    const { graph: _graph, type: _type, id, at, size, ...fields } = binding.value.args;
    return {
      alias,
      type,
      ...(typeof id === "string" ? { id } : {}),
      fields,
      ...(Array.isArray(at) ? { at: pointFromValue(at) } : {}),
      ...(Array.isArray(size) ? { size: pointFromValue(size) } : {}),
    };
  }

  return {
    alias,
    type: binding.value.callee,
    fields: structuredClone(binding.value.args),
  };
}

function nodeFromCreation(alias: string, creation: NodeCreation): Node {
  switch (creation.kind) {
    case "palette_node":
      return {
        alias,
        type: typeFromPaletteId(creation.palette),
        fields: structuredClone(creation.defaults ?? {}),
      };
    case "shortcut_node":
      return {
        alias,
        type: creation.constructor.callee,
        fields: structuredClone(creation.constructor.args),
      };
    default:
      return assertNever(creation);
  }
}

function typeFromPaletteId(palette: string): string {
  const tail = palette.split(/[.:/]/).filter(Boolean).at(-1);
  return tail ?? "PaletteNode";
}

function pointFromValue(value: Value[]): [number, number] | undefined {
  return value.length === 2 && typeof value[0] === "number" && typeof value[1] === "number"
    ? [value[0], value[1]]
    : undefined;
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

function isNodeCreation(value: Binding["value"]): value is NodeCreation {
  return (
    typeof value === "object" &&
    value !== null &&
    !Array.isArray(value) &&
    (value.kind === "palette_node" || value.kind === "shortcut_node")
  );
}

function isName(value: Expr | undefined): value is { kind: "name"; name: string } {
  return typeof value === "object" && value !== null && !Array.isArray(value) && value.kind === "name";
}

function isRef(value: Expr | undefined): value is Ref {
  return (
    typeof value === "object" &&
    value !== null &&
    !Array.isArray(value) &&
    (value.kind === "local" || value.kind === "member" || value.kind === "id")
  );
}

function symbolName(value: Expr | undefined): string | undefined {
  if (isName(value)) {
    return value.name;
  }
  if (isRef(value) && value.kind === "local") {
    return value.name;
  }
  if (typeof value === "string") {
    return value;
  }
  return undefined;
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

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}
