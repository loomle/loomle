import assert from "node:assert/strict";
import { Ajv2020 } from "ajv/dist/2020.js";
import { readFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { formatSalObject, parseSalObject, type Query } from "../../src/index.js";

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
