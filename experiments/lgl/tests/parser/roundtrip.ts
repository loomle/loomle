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
    name: "query palette entry",
    text: `bp = asset(path: "/Game/BP_LGLExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
query g
find palette entry "Print String"
`,
  },
  {
    name: "palette print string",
    text: `bp = asset(path: "/Game/BP_LGLExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
PrintString = node(palette: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString")
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
