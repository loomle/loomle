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
    text: `graph blueprint("/Game/BP_LGLExample"/EventGraph)

begin@A001: EventBeginPlay()
delay@A002: Delay({Duration: 1.0})
print@A003: PrintString({InString: "Ready"})

begin.Then -> delay.Exec/Completed -> print.Exec
`,
  },
  {
    name: "query find node",
    text: `query blueprint("/Game/BP_LGLExample"/EventGraph)

find node branch with pins, defaults
`,
  },
  {
    name: "query palette entry",
    text: `query blueprint("/Game/BP_LGLExample"/EventGraph)

find palette entry "Print String"
`,
  },
  {
    name: "palette print string",
    text: `palette blueprint("/Game/BP_LGLExample"/EventGraph)

PrintString = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString", title: "Print String", category: "Utilities/String"})
`,
  },
  {
    name: "patch insert delay",
    text: `patch blueprint("/Game/BP_LGLExample"/EventGraph) dry run

Delay = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay"})

delay = Delay({Duration: 1.0})
insert begin.Then -> delay.Exec/Completed -> print.Exec
move delay to (320, 0)
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
