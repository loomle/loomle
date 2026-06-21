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

const printPalette = await lgl.query(`${graphHeader}
query g
find palette entry "Print String"
with pins, defaults
page limit 10
`);
assert.equal(printPalette.diagnostics.length, 0);
assert.match(
  printPalette.text ?? "",
  /PrintString = node\(palette: "palette:blueprint:function:\/Script\/Engine.KismetSystemLibrary.PrintString", InString: ""\)/,
);
assert.match(printPalette.text ?? "", /PrintString.Exec = pin\(type: exec, direction: in\)/);
assert.match(printPalette.text ?? "", /PrintString.InString = pin\(type: string, direction: in, value: ""\)/);

const delayPalette = await lgl.query(`${graphHeader}
query g
find palette entry "Delay"
with pins, defaults
`);
assert.equal(delayPalette.diagnostics.length, 0);
assert.match(delayPalette.text ?? "", /Delay = delay\(duration: 1\)/);
assert.match(delayPalette.text ?? "", /Delay.Completed = pin\(type: exec, direction: out\)/);
console.log("[PASS] memory adapter returns copyable palette entries");

const dryRunPatch = await lgl.patch(`${graphHeader}
patch g dry run

delay = delay(duration: 1.0)
insert begin.Then -> delay.Exec/Completed -> print.Exec
move delay to (320, 0)
`);
assert.equal(dryRunPatch.diagnostics.length, 0);
assert.match(dryRunPatch.text ?? "", /delay = node\(graph: g, type: delay/);
assert.match(dryRunPatch.text ?? "", /begin.Then -> delay.Exec/);
assert.match(dryRunPatch.text ?? "", /delay.Completed -> print.Exec/);

const afterDryRun = adapter.getGraph(graph.target);
assert.deepEqual(afterDryRun?.edges, graph.edges);
assert.equal(afterDryRun?.nodes.some((node) => node.alias === "delay"), false);
console.log("[PASS] memory adapter dry run computes without mutating");

const applyPatch = await lgl.patch(`${graphHeader}
patch g

delay = delay(duration: 1.0)
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

const addConnectPatch = await lgl.patch(`${graphHeader}
patch g

print2 = node(palette: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString", InString: "Added")
add print2 delay.Completed -> print2.Exec
`);
assert.equal(addConnectPatch.diagnostics.length, 0);

const afterAddConnect = adapter.getGraph(graph.target);
assert.equal(afterAddConnect?.nodes.some((node) => node.alias === "print2"), true);
assert.equal(
  afterAddConnect?.edges.some(
    (edge) =>
      edge.from.node === "delay" &&
      edge.from.pin === "Completed" &&
      edge.to.node === "print2" &&
      edge.to.pin === "Exec",
  ),
  true,
);
console.log("[PASS] memory adapter adds and connects in one op");
