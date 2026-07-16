import { Ajv2020 } from "ajv/dist/2020.js";
import { readdir, readFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const packageRoot = join(here, "../../..");
const schemaPath = join(packageRoot, "schema/sal-object.schema.json");
const validFixtureDir = join(packageRoot, "fixtures/object");
const invalidFixtureDir = join(packageRoot, "fixtures/object-invalid");

const schema = JSON.parse(await readFile(schemaPath, "utf8"));
const ajv = new Ajv2020({ allErrors: true, strict: false });
const validate = ajv.compile(schema);

let failed = false;

async function validateFixtureDirectory(
  fixtureDir: string,
  expectedValid: boolean,
): Promise<void> {
  const fixtureNames = (await readdir(fixtureDir))
    .filter((name) => name.endsWith(".json"))
    .sort();

  for (const fixtureName of fixtureNames) {
    await validateFixture(fixtureDir, fixtureName, expectedValid);
  }
}

async function validateFixture(
  fixtureDir: string,
  fixtureName: string,
  expectedValid: boolean,
): Promise<void> {
  const fixturePath = join(fixtureDir, fixtureName);
  const fixture = JSON.parse(await readFile(fixturePath, "utf8"));
  const valid = validate(fixture);

  if (valid !== expectedValid) {
    failed = true;
    const expectation = expectedValid ? "valid" : "invalid";
    console.error(`[FAIL] ${fixtureName} should be ${expectation}`);
    if (!valid) {
      console.error(ajv.errorsText(validate.errors, { separator: "\n" }));
    }
    return;
  }

  const label = expectedValid ? "valid" : "invalid";
  console.log(`[PASS] ${fixtureName} ${label}`);
}

await validateFixtureDirectory(validFixtureDir, true);
await validateFixtureDirectory(invalidFixtureDir, false);

if (failed) {
  process.exitCode = 1;
}
