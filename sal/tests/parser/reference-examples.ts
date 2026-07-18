import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { checkBlueprintExamples } from "./example-conformance.js";

const here = dirname(fileURLToPath(import.meta.url));
const packageRoot = join(here, "../../..");
const results = await checkBlueprintExamples(packageRoot, "Reference");
let failed = false;

for (const result of results) {
  if (!result.ok) {
    failed = true;
    console.error(`[FAIL] ${result.fileName} ${result.stage}`);
    console.error(result.details);
    continue;
  }

  console.log(`[PASS] ${result.fileName}`);
}

if (failed) {
  process.exitCode = 1;
}
