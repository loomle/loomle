import assert from "node:assert/strict";
import {
  createLgl,
  createMemoryGraphAdapter,
  type Graph,
} from "../../src/index.js";

const graph: Graph = {
  kind: "graph",
  target: {
    domain: "blueprint",
    asset: "/Game/BP_LGLExample",
    graph: { kind: "name", name: "EventGraph" },
  },
  nodes: [
    {
      alias: "begin",
      id: "A001",
      type: "EventBeginPlay",
      fields: {},
    },
    {
      alias: "print",
      id: "A003",
      type: "PrintString",
      fields: { InString: "Ready" },
    },
  ],
  edges: [
    {
      from: { node: "begin", pin: "Then" },
      to: { node: "print", pin: "Exec" },
    },
  ],
  pins: [
    {
      node: "print",
      name: "InString",
      type: "string",
      direction: "in",
      value: "Ready",
    },
  ],
};

const adapter = createMemoryGraphAdapter({ domain: "blueprint", graphs: [graph] });
const lgl = createLgl({ adapters: [adapter] });

const graphHeader = `bp = asset(path: "/Game/BP_LGLExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)`;

const fullGraph = await lgl.query(`${graphHeader}
query g
`);
assert.equal(fullGraph.diagnostics.length, 0);
assert.match(fullGraph.text ?? "", /begin = node\(graph: g, type: EventBeginPlay, id: "A001"/);
assert.match(fullGraph.text ?? "", /print = node\(graph: g, type: PrintString, id: "A003"/);
console.log("[PASS] memory adapter returns full graph for empty query");

const printNodes = await lgl.query(`${graphHeader}
query g
find nodes
where type = PrintString
with pins
`);
assert.equal(printNodes.diagnostics.length, 0);
assert.doesNotMatch(printNodes.text ?? "", /begin = node/);
assert.match(printNodes.text ?? "", /print = node\(graph: g, type: PrintString, id: "A003"/);
assert.match(printNodes.text ?? "", /print.InString = pin\(type: string, direction: in, value: "Ready"\)/);
console.log("[PASS] memory adapter filters nodes and pins");

const dryRunPatch = await lgl.patch(`${graphHeader}
patch g dry run

Delay = palette(id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay")

delay = node(graph: g, type: Delay, Duration: 1.0)
insert begin.Then -> delay.Exec/Completed -> print.Exec
move delay to (320, 0)
`);
assert.equal(dryRunPatch.diagnostics.length, 0);
assert.match(dryRunPatch.text ?? "", /delay = node\(graph: g, type: Delay/);
assert.match(dryRunPatch.text ?? "", /begin.Then -> delay.Exec/);
assert.match(dryRunPatch.text ?? "", /delay.Completed -> print.Exec/);

const afterDryRun = adapter.getGraph(graph.target);
assert.deepEqual(afterDryRun?.edges, graph.edges);
assert.equal(afterDryRun?.nodes.some((node) => node.alias === "delay"), false);
console.log("[PASS] memory adapter dry run computes without mutating");

const applyPatch = await lgl.patch(`${graphHeader}
patch g

Delay = palette(id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay")

delay = node(graph: g, type: Delay, Duration: 1.0)
insert begin.Then -> delay.Exec/Completed -> print.Exec
move delay to (320, 0)
`);
assert.equal(applyPatch.diagnostics.length, 0);

const afterApply = adapter.getGraph(graph.target);
assert.equal(afterApply?.nodes.some((node) => node.alias === "delay"), true);
assert.equal(
  afterApply?.edges.some(
    (edge) =>
      edge.from.node === "begin" &&
      edge.from.pin === "Then" &&
      edge.to.node === "delay" &&
      edge.to.pin === "Exec",
  ),
  true,
);
assert.equal(
  afterApply?.edges.some(
    (edge) =>
      edge.from.node === "delay" &&
      edge.from.pin === "Completed" &&
      edge.to.node === "print" &&
      edge.to.pin === "Exec",
  ),
  true,
);
console.log("[PASS] memory adapter applies insert patch");

const pathQuery = await lgl.query(`${graphHeader}
query g
find path from begin.Then
`);
assert.equal(pathQuery.diagnostics.length, 0);
assert.match(pathQuery.text ?? "", /begin = node/);
assert.match(pathQuery.text ?? "", /delay = node/);
console.log("[PASS] memory adapter query observes applied patch");
