import { Ajv2020 } from "ajv/dist/2020.js";
import { readFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { formatLglObject, parseLglObject } from "../../src/index.js";

const here = dirname(fileURLToPath(import.meta.url));
const packageRoot = join(here, "../../..");
const schemaPath = join(packageRoot, "schema/lgl-object.schema.json");
const schema = JSON.parse(await readFile(schemaPath, "utf8"));
const ajv = new Ajv2020({ allErrors: true, strict: false });
const validate = ajv.compile(schema);

const cases: Array<{ name: string; text: string }> = [
  {
    name: "graph begin delay print",
    text: `bp = asset(path: "/Game/BP_LGLExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)

begin = node(graph: g, type: EventBeginPlay, id: "A001")
delay = node(graph: g, type: Delay, id: "A002", Duration: 1.0)
print = node(graph: g, type: PrintString, id: "A003", InString: "Ready")

begin.Then -> delay.Exec/Completed -> print.Exec
`,
  },
  {
    name: "query find nodes",
    text: `bp = asset(path: "/Game/BP_LGLExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
query g
find nodes
where name = branch
with pins, defaults
`,
  },
  {
    name: "query assets",
    text: `query asset
find assets "door"
where root = "/Game" and type = blueprint
with registryTags
order by score desc, path asc
page limit 10
`,
  },
  {
    name: "asset result",
    text: `door = asset(path: "/Game/Blueprints/BP_Door.BP_Door", type: blueprint, class: "/Script/Engine.Blueprint", domains: [asset, blueprint], loaded: false, registryTags: {ParentClass: "/Script/Engine.Actor"}, score: 98)
`,
  },
  {
    name: "query blueprint members",
    text: `bpAsset = asset(path: "/Game/BP_Door.BP_Door", type: blueprint)
bp = blueprint(asset: bpAsset)
query bp
find members "Health"
where kind = variable
page limit 10
`,
  },
  {
    name: "blueprint result",
    text: `door = blueprint(asset: "/Game/BP_Door.BP_Door", parent: "/Script/Engine.Actor")
door.Health = variable(type: float, default: 100.0, category: "Stats")
door.OpenDoor = function(inputs: {speed: float}, outputs: {success: bool}, pure: false)
door.Trigger = component(class: "/Script/Engine.BoxComponent", boxExtent: [100, 100, 200])
`,
  },
  {
    name: "patch blueprint members",
    text: `bpAsset = asset(path: "/Game/BP_Door.BP_Door", type: blueprint)
door = blueprint(asset: bpAsset)
patch door dry run

door.Health = variable(type: float, default: 100.0, category: "Stats")
add door.Health
set door.parent = "/Script/Engine.Character"
remove door.OldHealth
rename door.Health to MaxHealth
move Root.Trigger after Root.Mesh
`,
  },
  {
    name: "query widget tree",
    text: `widgetAsset = asset(path: "/Game/UI/WBP_Menu.WBP_Menu", type: widget)
w = widget(asset: widgetAsset, root: mainCanvas)
query w
find tree
`,
  },
  {
    name: "query widgets",
    text: `widgetAsset = asset(path: "/Game/UI/WBP_Menu.WBP_Menu", type: widget)
w = widget(asset: widgetAsset, root: mainCanvas)
query w
find widgets "Start"
where type = Button
page limit 10
`,
  },
  {
    name: "query widget palette entry",
    text: `widgetAsset = asset(path: "/Game/UI/WBP_Menu.WBP_Menu", type: widget)
w = widget(asset: widgetAsset, root: mainCanvas)
query w
find palette entry "Button"
with defaults
page limit 10
`,
  },
  {
    name: "widget result",
    text: `menu = widget(asset: "/Game/UI/WBP_Menu.WBP_Menu", root: mainCanvas)
mainCanvas = CanvasPanel()
mainCanvas.stack = VerticalBox()
stack.title = TextBlock(text: "Main Menu", fontSize: 32)
stack.start = Button(text: "Start")
`,
  },
  {
    name: "patch widget tree",
    text: `widgetAsset = asset(path: "/Game/UI/WBP_Menu.WBP_Menu", type: widget)
w = widget(asset: widgetAsset, root: mainCanvas)
patch w dry run

stack.help = Button(text: "Help")
add stack.help
set title.text = "Main Menu"
move help before start
remove quit
`,
  },
  {
    name: "query palette entry",
    text: `bp = asset(path: "/Game/BP_LGLExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
query g
find palette entry "Print String"
with pins, defaults
page limit 10
`,
  },
  {
    name: "query find path to pin",
    text: `bp = asset(path: "/Game/BP_LGLExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
query g
find path to print.Exec
`,
  },
  {
    name: "query palette entry with pin context",
    text: `bp = asset(path: "/Game/BP_LGLExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
query g
find palette entry "Branch" from begin.Then
with pins
order by name asc
page limit 5
page after "offset:0"
`,
  },
  {
    name: "palette print string",
    text: `bp = asset(path: "/Game/BP_LGLExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
PrintString = node(palette: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString", InString: "")
PrintString.Exec = pin(type: exec, direction: in)
PrintString.InString = pin(type: string, direction: in, value: "")
PrintString.Then = pin(type: exec, direction: out)
`,
  },
  {
    name: "widget palette result",
    text: `widgetAsset = asset(path: "/Game/UI/WBP_Menu.WBP_Menu", type: widget)
w = widget(asset: widgetAsset, root: root)
Button = Button()
InventorySlot = widget(class: "/Game/UI/WBP_InventorySlot.WBP_InventorySlot_C")
PluginFancy = widget(palette: "widget.palette:plugin-fancy")
`,
  },
  {
    name: "patch insert delay",
    text: `bp = asset(path: "/Game/BP_LGLExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
patch g dry run

delay = delay(duration: 1.0)
insert begin.Then -> delay.Exec/Completed -> print.Exec
move delay to (320, 0)
`,
  },
  {
    name: "patch add and connect",
    text: `bp = asset(path: "/Game/BP_LGLExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
patch g

print = node(palette: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString", InString: "Ready")
add print begin.Then -> print.Exec
`,
  },
  {
    name: "patch maintenance ops",
    text: `bp = asset(path: "/Game/BP_LGLExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
patch g

add print = node(palette: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString", InString: "Ready")
begin.Then -> print.Exec
set print.InString = "Updated"
disconnect print.InString
remove print
move delay by (120, -40)
reconstruct delay preserve links
`,
  },
];

let failed = false;

for (const testCase of cases) {
  const parsed = parseLglObject(testCase.text);
  if (!parsed.object || parsed.diagnostics.length > 0) {
    failed = true;
    console.error(`[FAIL] ${testCase.name} parse`);
    console.error(JSON.stringify(parsed.diagnostics, null, 2));
    continue;
  }

  if (!validate(parsed.object)) {
    failed = true;
    console.error(`[FAIL] ${testCase.name} schema`);
    console.error(ajv.errorsText(validate.errors, { separator: "\n" }));
    continue;
  }

  const formatted = formatLglObject(parsed.object);
  const reparsed = parseLglObject(formatted);
  if (!reparsed.object || reparsed.diagnostics.length > 0) {
    failed = true;
    console.error(`[FAIL] ${testCase.name} reparse`);
    console.error(formatted);
    console.error(JSON.stringify(reparsed.diagnostics, null, 2));
    continue;
  }

  if (!validate(reparsed.object)) {
    failed = true;
    console.error(`[FAIL] ${testCase.name} reparse schema`);
    console.error(ajv.errorsText(validate.errors, { separator: "\n" }));
    continue;
  }

  console.log(`[PASS] ${testCase.name}`);
}

if (failed) {
  process.exitCode = 1;
}
