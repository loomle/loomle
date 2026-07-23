import assert from "node:assert/strict";
import { Ajv2020 } from "ajv/dist/2020.js";
import { readFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { formatSalObject, parseSalObject, type Patch, type Query } from "../../src/index.js";

const here = dirname(fileURLToPath(import.meta.url));
const packageRoot = join(here, "../../..");
const schema = JSON.parse(await readFile(join(packageRoot, "schema/sal-object.schema.json"), "utf8"));
const ajv = new Ajv2020({ allErrors: true, strict: false });
ajv.addSchema(schema);
const validate = ajv.compile({ $ref: `${schema.$id}#/$defs/SalObject` });

const cases: Array<{ name: string; text: string }> = [
  {
    name: "empty object text",
    text: "",
  },
  {
    name: "query and patch remain valid object aliases",
    text: `query = asset(path: "/Game/Query.Query")
patch = asset(path: "/Game/Patch.Patch")`,
  },
  {
    name: "asset collection query",
    text: `query asset
assets "door"
where path ~= "/Game" and loaded = false
with schema
order by name asc
page limit 10`,
  },
  {
    name: "grouped query conditions",
    text: `query asset
assets
where loaded = false and (path ~= "/Game" or name ~= "Door") and not deprecated`,
  },
  {
    name: "blueprint summary with nested target",
    text: `bp = blueprint(asset: "/Game/Doors/BP_Door.BP_Door")
query bp
summary`,
  },
  {
    name: "bare StateTree target read with schema",
    text: `omle = asset(path: "/Game/AI/ST_Omle.ST_Omle")
query omle
with schema`,
  },
  {
    name: "StateTree tree rooted at a stable State",
    text: `omle = asset(path: "/Game/AI/ST_Omle.ST_Omle")
query omle
tree state@ROOT depth 8`,
  },
  {
    name: "StateTree collection search",
    text: `omle = asset(path: "/Game/AI/ST_Omle.ST_Omle")
query omle
parameters "speed"
page limit 20`,
  },
  {
    name: "StateTree exact compound Parameter id",
    text: `omle = asset(path: "/Game/AI/ST_Omle.ST_Omle")
query omle
parameter@CONTAINER/SPEED
with schema`,
  },
  {
    name: "StateTree Palette search bound to a destination",
    text: `omle = asset(path: "/Game/AI/ST_Omle.ST_Omle")
query omle
palette entries "Follow" to state@COMPANION.Tasks
page limit 10`,
  },
  {
    name: "exact StateTree Palette entry bound to a destination",
    text: `omle = asset(path: "/Game/AI/ST_Omle.ST_Omle")
query omle
palette @P_Omle.Follow[Task] to state@COMPANION.Tasks
with schema`,
  },
  {
    name: "full object bindings remain reusable target locators",
    text: `doorAsset = asset(path: "/Game/Doors/BP_Door.BP_Door", type: "/Script/Engine.Blueprint")
bp = blueprint(asset: doorAsset, id: "blueprint-guid", type: BPTYPE_Normal, Status: BS_UpToDate)
eventGraph = graph(asset: bp, id: "graph-guid", name: EventGraph, type: GT_Ubergraph)
query eventGraph
summary`,
  },
  {
    name: "graph collection query",
    text: `bp = blueprint(asset: "/Game/Doors/BP_Door.BP_Door")
eventGraph = graph(asset: bp, name: "EventGraph")
query eventGraph
nodes "Print"
where type = "/Script/BlueprintGraph.K2Node_CallFunction"
with layout
page after "offset:10"`,
  },
  {
    name: "exact stable reference query",
    text: `bp = blueprint(asset: "/Game/Doors/BP_Door.BP_Door")
eventGraph = graph(asset: bp, name: "EventGraph")
query eventGraph
node@4F781A
with schema`,
  },
  {
    name: "graph relationship query",
    text: `bp = blueprint(asset: "/Game/Doors/BP_Door.BP_Door")
eventGraph = graph(asset: bp, name: "EventGraph")
query eventGraph
exec flow from pin@A001 depth 4`,
  },
  {
    name: "local reference query",
    text: `bp = blueprint(asset: "/Game/Doors/BP_Door.BP_Door")
query bp
references to variable@HEALTH
page limit 50`,
  },
  {
    name: "project member reference query",
    text: `bp = blueprint(asset: "/Game/Doors/BP_Door.BP_Door")
eventGraph = graph(asset: bp, name: "EventGraph")
query eventGraph
references to node@CALL.FunctionReference in project
page limit 50
page after "opaque-cursor"`,
  },
  {
    name: "widget tree query",
    text: `menu = blueprint(asset: "/Game/UI/WBP_Menu.WBP_Menu")
query menu
tree widget@ROOT depth 20
with schema`,
  },
  {
    name: "palette query with pin context",
    text: `bp = blueprint(asset: "/Game/Doors/BP_Door.BP_Door")
eventGraph = graph(asset: bp, name: "EventGraph")
query eventGraph
palette entries "Branch" from pin@A001
with schema
page limit 5`,
  },
  {
    name: "ordered object text",
    text: `# execution entry
begin = node(id: "N1", type: "/Script/BlueprintGraph.K2Node_Event")
begin.Then = pin(id: "P1", direction: out)
print = node(id: "N2", type: "/Script/BlueprintGraph.K2Node_CallFunction", FunctionReference: "PrintString")
print.execute = pin(id: "P2", direction: in)
pin@P1 -> pin@P2
###
continuation graph: graph@NEXT
open it only when needed
###`,
  },
  {
    name: "indexed local member binding",
    text: `task = node(id: "TASK")
task.Instance.Targets[0].Location = object(type: "/Script/CoreUObject.Vector")`,
  },
  {
    name: "opaque comment whitespace and delimiter text",
    text: "###\n  preserve this whitespace" + "  \n###\n# ###",
  },
  {
    name: "graph patch",
    text: `bp = blueprint(asset: "/Game/Doors/BP_Door.BP_Door")
eventGraph = graph(asset: bp, name: "EventGraph")
patch eventGraph dry run

delay = node(palette: "Delay", Duration: 1.0)
add delay
connect pin@P1 -> delay.execute
set node@N2.NodeComment = "Print after delay"
move node@N2 by (160, -40)
invoke node@N2 AddPin() as subpins.X: extraPin
compile
save`,
  },
  {
    name: "insert and wrapper patch",
    text: `menu = blueprint(asset: "/Game/UI/WBP_Menu.WBP_Menu")
patch menu

box = widget(palette: "VerticalBox")
wrap [widget@TITLE, widget@START] with box
replace widget@OLD with box
replace widget@PLACEHOLDER with widget@EXISTING
save`,
  },
  {
    name: "StateTree bind and unbind patch",
    text: `omle = asset(path: "/Game/AI/ST_Omle.ST_Omle")
patch omle dry run

bind parameter@CONTAINER/POINTS[0].X -> node@TASK.Instance.Targets[1].X
unbind node@PRODUCER.Instance.OnFinished -> transition@FAILED.DelegateListener`,
  },
];

for (const testCase of cases) {
  const parsed = parseSalObject(testCase.text);
  assert.ok(parsed.object, `${testCase.name}: ${JSON.stringify(parsed.diagnostics)}`);
  assert.deepEqual(parsed.diagnostics, [], testCase.name);
  assert.equal(validate(parsed.object), true, `${testCase.name}: ${ajv.errorsText(validate.errors)}`);

  const formatted = formatSalObject(parsed.object);
  const reparsed = parseSalObject(formatted);
  assert.deepEqual(reparsed.diagnostics, [], `${testCase.name} formatted parse`);
  assert.deepEqual(reparsed.object, parsed.object, `${testCase.name} normalized round trip`);
  console.log(`[PASS] ${testCase.name}`);
}

const normalizedReference = parseSalObject(`bp = blueprint(asset: "/Game/Doors/BP_Door.BP_Door")
query bp
references to node@CALL.FunctionReference in project
page limit 50`);
assert.deepEqual((normalizedReference.object as Query).operation, {
  kind: "references",
  target: {
    kind: "member",
    object: { kind: "node", id: "CALL" },
    path: ["FunctionReference"],
  },
  scope: "project",
});
assert.deepEqual((normalizedReference.object as Query).page, { limit: 50 });
console.log("[PASS] references text has the confirmed normalized shape");

const bareTarget = parseSalObject(`omle = asset(path: "/Game/AI/ST_Omle.ST_Omle")
query omle
with schema`);
assert.deepEqual((bareTarget.object as Query).operation, { kind: "target" });
assert.deepEqual((bareTarget.object as Query).with, ["schema"]);
console.log("[PASS] bare exact-target read has the shared target operation");

const indexedReference = parseSalObject(`omle = asset(path: "/Game/AI/ST_Omle.ST_Omle")
query omle
references to parameter@CONTAINER/POINTS[0].X`);
assert.deepEqual((indexedReference.object as Query).operation, {
  kind: "references",
  target: {
    kind: "member",
    object: { kind: "parameter", id: "CONTAINER/POINTS" },
    path: [0, "X"],
  },
});
console.log("[PASS] compound Parameter ids remain distinct from indexed member paths");

const destinationPalette = parseSalObject(`omle = asset(path: "/Game/AI/ST_Omle.ST_Omle")
query omle
palette entries "Follow" to state@COMPANION.Tasks`);
assert.deepEqual((destinationPalette.object as Query).operation, {
  kind: "palette_entries",
  text: "Follow",
  to: {
    kind: "member",
    object: { kind: "state", id: "COMPANION" },
    path: ["Tasks"],
  },
});
console.log("[PASS] StateTree Palette search preserves its exact destination");

const graphPalette = parseSalObject(`g = graph(asset: "/Game/BP_Test.BP_Test", name: "EventGraph")
query g
palette entries "Branch" to pin@INPUT`);
assert.deepEqual((graphPalette.object as Query).operation, {
  kind: "palette_entries",
  text: "Branch",
  pinContext: { direction: "to", pin: { kind: "pin", id: "INPUT" } },
});
console.log("[PASS] Graph Palette pin context remains distinct from StateTree destination context");

const stateTreePatch = parseSalObject(`omle = asset(path: "/Game/AI/ST_Omle.ST_Omle")
patch omle
bind parameter@CONTAINER/POINTS[0].X -> node@TASK.Instance.Targets[1].X
unbind node@PRODUCER.Instance.OnFinished -> transition@FAILED.DelegateListener`);
assert.deepEqual((stateTreePatch.object as Patch).statements, [
  {
    kind: "bind",
    from: {
      kind: "member",
      object: { kind: "parameter", id: "CONTAINER/POINTS" },
      path: [0, "X"],
    },
    to: {
      kind: "member",
      object: { kind: "node", id: "TASK" },
      path: ["Instance", "Targets", 1, "X"],
    },
  },
  {
    kind: "unbind",
    from: {
      kind: "member",
      object: { kind: "node", id: "PRODUCER" },
      path: ["Instance", "OnFinished"],
    },
    to: {
      kind: "member",
      object: { kind: "transition", id: "FAILED" },
      path: ["DelegateListener"],
    },
  },
]);
console.log("[PASS] bind and unbind preserve exact indexed endpoints in data-flow order");
