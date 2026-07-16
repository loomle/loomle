import assert from "node:assert/strict";
import {
  createSal,
  createMemoryGraphAdapter,
  type Graph,
} from "../../src/index.js";

const graph: Graph = {
  kind: "graph",
  target: {
    domain: "blueprint",
    asset: "/Game/BP_SALExample",
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
      node: "begin",
      name: "Then",
      type: "exec",
      direction: "out",
    },
    {
      node: "print",
      name: "Exec",
      type: "exec",
      direction: "in",
    },
    {
      node: "print",
      name: "InString",
      type: "string",
      direction: "in",
      value: "Ready",
    },
    {
      node: "print",
      name: "Then",
      type: "exec",
      direction: "out",
    },
  ],
};

const adapter = createMemoryGraphAdapter({ domain: "blueprint", graphs: [graph] });
const sal = createSal({ adapters: [adapter] });

const graphHeader = `bp = asset(path: "/Game/BP_SALExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)`;

const fullGraph = await sal.query(`${graphHeader}
query g
`);
assert.equal(fullGraph.diagnostics.length, 0);
assert.match(fullGraph.text ?? "", /begin = node\(graph: g, type: EventBeginPlay, id: "A001"/);
assert.match(fullGraph.text ?? "", /print = node\(graph: g, type: PrintString, id: "A003"/);
console.log("[PASS] memory adapter returns full graph for empty query");

const printNodes = await sal.query(`${graphHeader}
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

const printPalette = await sal.query(`${graphHeader}
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

const delayPalette = await sal.query(`${graphHeader}
query g
find palette entry "Delay"
with pins, defaults
`);
assert.equal(delayPalette.diagnostics.length, 0);
assert.match(delayPalette.text ?? "", /Delay = delay\(duration: 1\)/);
assert.match(delayPalette.text ?? "", /Delay.Completed = pin\(type: exec, direction: out\)/);
console.log("[PASS] memory adapter returns copyable palette entries");

const branchFromExec = await sal.query(`${graphHeader}
query g
find palette entry "Branch" from begin.Then
with pins
`);
assert.equal(branchFromExec.diagnostics.length, 0);
assert.match(branchFromExec.text ?? "", /Branch = branch\(\)/);
assert.match(branchFromExec.text ?? "", /Branch.Exec = pin\(type: exec, direction: in\)/);
assert.doesNotMatch(branchFromExec.text ?? "", /PrintString = node/);

const branchToExec = await sal.query(`${graphHeader}
query g
find palette entry "Branch" to print.Exec
with pins
`);
assert.equal(branchToExec.diagnostics.length, 0);
assert.match(branchToExec.text ?? "", /Branch = branch\(\)/);
assert.match(branchToExec.text ?? "", /Branch.Then = pin\(type: exec, direction: out\)/);

const missingPinContext = await sal.query(`${graphHeader}
query g
find palette entry "Branch" from begin.Missing
`);
assert.equal(missingPinContext.text, undefined);
assert.equal(missingPinContext.diagnostics[0]?.code, "unknown_pin_context");
console.log("[PASS] memory adapter filters palette entries by pin context");

const orderedNodes = await sal.query(`${graphHeader}
query g
find nodes
order by type desc
page limit 1
`);
assert.equal(orderedNodes.diagnostics.length, 0);
assert.match(orderedNodes.text ?? "", /print = node\(graph: g, type: PrintString/);
assert.doesNotMatch(orderedNodes.text ?? "", /begin = node/);
assert.equal(orderedNodes.page?.next, "offset:1");

const firstPalettePage = await sal.query(`${graphHeader}
query g
find palette entry
order by name asc
page limit 1
`);
assert.equal(firstPalettePage.diagnostics.length, 0);
assert.match(firstPalettePage.text ?? "", /Branch = branch\(\)/);
assert.equal(firstPalettePage.page?.next, "offset:1");

const pagedPalette = await sal.query(`${graphHeader}
query g
find palette entry
order by name asc
page limit 1
page after "${firstPalettePage.page?.next}"
`);
assert.equal(pagedPalette.diagnostics.length, 0);
assert.match(pagedPalette.text ?? "", /Delay = delay\(duration: 1\)/);
assert.doesNotMatch(pagedPalette.text ?? "", /Branch = branch\(\)/);
assert.equal(pagedPalette.page?.next, "offset:2");

const finalPalettePage = await sal.query(`${graphHeader}
query g
find palette entry
order by name asc
page limit 1
page after "${pagedPalette.page?.next}"
`);
assert.equal(finalPalettePage.diagnostics.length, 0);
assert.match(finalPalettePage.text ?? "", /PrintString = node\(palette:/);
assert.equal(finalPalettePage.page, undefined);
console.log("[PASS] memory adapter applies query ordering and pagination");

const dryRunPatch = await sal.patch(`${graphHeader}
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

const applyPatch = await sal.patch(`${graphHeader}
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

const pathQuery = await sal.query(`${graphHeader}
query g
find path from begin.Then
`);
assert.equal(pathQuery.diagnostics.length, 0);
assert.match(pathQuery.text ?? "", /begin = node/);
assert.match(pathQuery.text ?? "", /delay = node/);
console.log("[PASS] memory adapter query observes applied patch");

const addConnectPatch = await sal.patch(`${graphHeader}
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
console.log("[PASS] memory adapter lowers add-connect sugar into add and connect ops");

const maintenancePatch = await sal.patch(`${graphHeader}
patch g

set print.InString = "Updated"
disconnect delay.Completed
move print by (10, 5)
reconstruct print preserve links
remove print2
`);
assert.equal(maintenancePatch.diagnostics.length, 0);

const afterMaintenance = adapter.getGraph(graph.target);
const updatedPrint = afterMaintenance?.nodes.find((node) => node.alias === "print");
assert.equal(updatedPrint?.fields.InString, "Updated");
assert.deepEqual(updatedPrint?.at, [10, 5]);
assert.equal(afterMaintenance?.nodes.some((node) => node.alias === "print2"), false);
assert.equal(
  afterMaintenance?.edges.some(
    (edge) =>
      (edge.from.node === "delay" && edge.from.pin === "Completed") ||
      (edge.to.node === "delay" && edge.to.pin === "Completed"),
  ),
  false,
);
console.log("[PASS] memory adapter applies maintenance patch ops");

const missingAddBinding = await sal.patch(`${graphHeader}
patch g

add missingNode
`);
assert.equal(missingAddBinding.diagnostics[0]?.code, "unknown_node_binding");

const missingInsertBinding = await sal.patch(`${graphHeader}
patch g

insert begin.Then -> missingNode.Exec/Completed -> delay.Exec
`);
assert.equal(missingInsertBinding.diagnostics[0]?.code, "unknown_node_binding");

const missingInsertEdge = await sal.patch(`${graphHeader}
patch g

delay2 = delay(duration: 1.0)
insert print.Then -> delay2.Exec/Completed -> begin.Exec
`);
assert.equal(missingInsertEdge.diagnostics[0]?.code, "missing_insert_edge");

const missingDisconnectEdge = await sal.patch(`${graphHeader}
patch g

disconnect begin.Then -> print.Exec
`);
assert.equal(missingDisconnectEdge.diagnostics[0]?.code, "missing_edge");

const forwardReferencePatch = await sal.patch(`${graphHeader}
patch g

delay2 = delay(duration: 2.0)
connect print.Then -> delay2.Exec
add delay2
`);
assert.equal(forwardReferencePatch.diagnostics.length, 0);
const afterForwardReference = adapter.getGraph(graph.target);
assert.equal(afterForwardReference?.nodes.some((node) => node.alias === "delay2"), true);
assert.equal(
  afterForwardReference?.edges.some(
    (edge) =>
      edge.from.node === "print" &&
      edge.from.pin === "Then" &&
      edge.to.node === "delay2" &&
      edge.to.pin === "Exec",
  ),
  true,
);

const atomicFailure = await sal.patch(`${graphHeader}
patch g

set print.InString = "ShouldNotApply"
connect missingNode.Then -> print.Exec
`);
assert.equal(atomicFailure.text, undefined);
assert.equal(atomicFailure.diagnostics[0]?.code, "unknown_node");
const afterAtomicFailure = adapter.getGraph(graph.target);
assert.equal(afterAtomicFailure?.nodes.find((node) => node.alias === "print")?.fields.InString, "Updated");
console.log("[PASS] memory adapter reports patch validation diagnostics");
