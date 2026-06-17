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

const fullGraph = await lgl.query(`query blueprint("/Game/BP_LGLExample"/EventGraph)
`);
assert.equal(fullGraph.diagnostics.length, 0);
assert.match(fullGraph.text ?? "", /begin@A001: EventBeginPlay/);
assert.match(fullGraph.text ?? "", /print@A003: PrintString/);
console.log("[PASS] memory adapter returns full graph for empty query");

const printNodes = await lgl.query(`query blueprint("/Game/BP_LGLExample"/EventGraph)

find nodes where type = PrintString with pins
`);
assert.equal(printNodes.diagnostics.length, 0);
assert.doesNotMatch(printNodes.text ?? "", /begin@A001/);
assert.match(printNodes.text ?? "", /print@A003: PrintString/);
assert.match(printNodes.text ?? "", /print.InString: string in/);
console.log("[PASS] memory adapter filters nodes and pins");

const dryRunPatch = await lgl.patch(`patch blueprint("/Game/BP_LGLExample"/EventGraph) dry run

Delay = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay"})

delay = Delay({Duration: 1.0})
insert begin.Then -> delay.Exec/Completed -> print.Exec
move delay to (320, 0)
`);
assert.equal(dryRunPatch.diagnostics.length, 0);
assert.match(dryRunPatch.text ?? "", /delay: Delay/);
assert.match(dryRunPatch.text ?? "", /begin.Then -> delay.Exec/);
assert.match(dryRunPatch.text ?? "", /delay.Completed -> print.Exec/);

const afterDryRun = adapter.getGraph(graph.target);
assert.deepEqual(afterDryRun?.edges, graph.edges);
assert.equal(afterDryRun?.nodes.some((node) => node.alias === "delay"), false);
console.log("[PASS] memory adapter dry run computes without mutating");

const applyPatch = await lgl.patch(`patch blueprint("/Game/BP_LGLExample"/EventGraph)

Delay = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay"})

delay = Delay({Duration: 1.0})
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

const pathQuery = await lgl.query(`query blueprint("/Game/BP_LGLExample"/EventGraph)

find path from begin.Then
`);
assert.equal(pathQuery.diagnostics.length, 0);
assert.match(pathQuery.text ?? "", /begin@A001/);
assert.match(pathQuery.text ?? "", /delay: Delay/);
console.log("[PASS] memory adapter query observes applied patch");
