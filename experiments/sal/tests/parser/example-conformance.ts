import { Ajv2020 } from "ajv/dist/2020.js";
import { readFile } from "node:fs/promises";
import { join } from "node:path";
import { formatSalObject, parseSalObject } from "../../src/index.js";

export interface ExampleCheckResult {
  fileName: string;
  ok: boolean;
  stage?: "parse" | "schema" | "formatted_parse" | "formatted_schema";
  details?: string;
}

export async function checkBlueprintExamples(
  packageRoot: string,
  section: string,
): Promise<ExampleCheckResult[]> {
  const examplesDir = join(packageRoot, "examples/blueprint");
  const readmePath = join(examplesDir, "README.md");
  const schemaPath = join(packageRoot, "schema/sal-object.schema.json");
  const schema = JSON.parse(await readFile(schemaPath, "utf8"));
  const ajv = new Ajv2020({ allErrors: true, strict: false });
  const validate = ajv.compile(schema);
  const examples = extractExamples(await readFile(readmePath, "utf8"), section);
  const results: ExampleCheckResult[] = [];

  for (const fileName of examples) {
    const source = await readFile(join(examplesDir, fileName), "utf8");
    const requestDocument = source.split(/^---$/m)[0].trim();
    const parsed = parseSalObject(requestDocument);

    if (!parsed.object || parsed.diagnostics.length > 0) {
      results.push({
        fileName,
        ok: false,
        stage: "parse",
        details: JSON.stringify(parsed.diagnostics, null, 2),
      });
      continue;
    }

    if (!validate(parsed.object)) {
      results.push({
        fileName,
        ok: false,
        stage: "schema",
        details: ajv.errorsText(validate.errors, { separator: "\n" }),
      });
      continue;
    }

    const formatted = formatSalObject(parsed.object);
    const reparsed = parseSalObject(formatted);
    if (!reparsed.object || reparsed.diagnostics.length > 0) {
      results.push({
        fileName,
        ok: false,
        stage: "formatted_parse",
        details: `${formatted}\n${JSON.stringify(reparsed.diagnostics, null, 2)}`,
      });
      continue;
    }

    if (!validate(reparsed.object)) {
      results.push({
        fileName,
        ok: false,
        stage: "formatted_schema",
        details: ajv.errorsText(validate.errors, { separator: "\n" }),
      });
      continue;
    }

    results.push({ fileName, ok: true });
  }

  return results;
}

export function extractExamples(readme: string, section: string): string[] {
  const lines = readme.split(/\r?\n/);
  const examples: string[] = [];
  let inSection = false;

  for (const line of lines) {
    if (line.startsWith("## ")) {
      if (line === `## ${section}`) {
        inSection = true;
        continue;
      }
      if (inSection) {
        break;
      }
    }

    if (!inSection) {
      continue;
    }

    const match = /^- `([^`]+)`$/.exec(line.trim());
    if (match) {
      examples.push(match[1]);
    }
  }

  if (examples.length === 0) {
    throw new Error(`No ${section} examples listed in examples/blueprint/README.md.`);
  }

  return examples;
}
