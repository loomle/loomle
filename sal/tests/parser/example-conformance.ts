import { Ajv2020 } from "ajv/dist/2020.js";
import { readFile } from "node:fs/promises";
import { join } from "node:path";
import {
  formatSalObject,
  parseSalObject,
  parseSalResultText,
  type ParsedResultText,
  type SalObject,
} from "../../src/index.js";

export interface ExampleCheckResult {
  fileName: string;
  ok: boolean;
  stage?:
    | "parse"
    | "schema"
    | "domain_contract"
    | "formatted_parse"
    | "formatted_schema"
    | "result_parse"
    | "result_domain_contract";
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
    const [requestSource, resultSource] = source.split(/^---$/m, 2);
    const requestDocument = requestSource.trim();
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

    const domainErrors = validateDomainContract(parsed.object);
    if (domainErrors.length > 0) {
      results.push({
        fileName,
        ok: false,
        stage: "domain_contract",
        details: domainErrors.join("\n"),
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

    if (resultSource?.trim()) {
      const parsedResult = parseSalResultText(resultSource.trim());
      if (!parsedResult.result || parsedResult.diagnostics.length > 0) {
        results.push({
          fileName,
          ok: false,
          stage: "result_parse",
          details: JSON.stringify(parsedResult.diagnostics, null, 2),
        });
        continue;
      }
      const resultDomainErrors = validateResultDomainContract(parsedResult.result);
      if (resultDomainErrors.length > 0) {
        results.push({
          fileName,
          ok: false,
          stage: "result_domain_contract",
          details: resultDomainErrors.join("\n"),
        });
        continue;
      }
    }

    results.push({ fileName, ok: true });
  }

  return results;
}

const canonicalGuid =
  /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

function validateDomainContract(object: SalObject): string[] {
  const errors: string[] = [];
  const value = object as unknown as Record<string, unknown>;
  const domain = requestDomain(value);

  if (value.kind === "patch" && (domain === "graph" || domain === "widget")) {
    const statements = Array.isArray(value.statements) ? value.statements : [];
    for (const statement of statements) {
      if (!isRecord(statement)) continue;
      if (statement.kind === "compile" || statement.kind === "save") {
        errors.push(
          `${domain} Patch cannot ${statement.kind}; use the returned Blueprint Target handoff.`,
        );
      }
    }
  }

  validateStableRefs(value, errors, domain === "graph");
  validatePinOperationRefs(value, errors);
  return errors;
}

function validateResultDomainContract(result: ParsedResultText): string[] {
  const errors: string[] = [];
  const domain = result.targetContext === "unresolved_target"
    ? undefined
    : result.target.target.domain;
  if (result.object) {
    validateStableRefs(
      result.object as unknown as Record<string, unknown>,
      errors,
      domain === "graph",
    );
    validatePinOperationRefs(result.object as unknown as Record<string, unknown>, errors);
  }
  return errors;
}

function requestDomain(value: Record<string, unknown>): string | undefined {
  if (!isRecord(value.target) || !isRecord(value.target.target)) return undefined;
  return typeof value.target.target.domain === "string"
    ? value.target.target.domain
    : undefined;
}

function validateStableRefs(
  value: unknown,
  errors: string[],
  requireGuidComponents: boolean,
): void {
  if (Array.isArray(value)) {
    for (const item of value) validateStableRefs(item, errors, requireGuidComponents);
    return;
  }
  if (!isRecord(value)) return;

  if (value.kind === "stable_ref") {
    const path = Array.isArray(value.identityPath)
      ? value.identityPath.filter((segment): segment is string => typeof segment === "string")
      : [];
    if (value.semanticTag === "pin" && path.length !== 2) {
      errors.push("A Graph Pin StableRef must use NodeGuid/PinId.");
    }
    if (requireGuidComponents && path.some((segment) => !canonicalGuid.test(segment))) {
      errors.push(`Graph StableRef contains a non-canonical Guid: ${path.join("/")}.`);
    }
  }

  for (const child of Object.values(value)) {
    validateStableRefs(child, errors, requireGuidComponents);
  }
}

function validatePinOperationRefs(value: unknown, errors: string[]): void {
  if (Array.isArray(value)) {
    for (const item of value) validatePinOperationRefs(item, errors);
    return;
  }
  if (!isRecord(value)) return;

  const pinFields = value.kind === "connect" || value.kind === "disconnect"
    ? ["from", "to"]
    : value.kind === "break"
      ? ["target"]
      : value.kind === "insert"
        ? ["from", "input", "output", "to"]
        : value.kind === undefined && "from" in value && "to" in value
          ? ["from", "to"]
          : [];
  for (const field of pinFields) {
    const ref = value[field];
    if (isRecord(ref)
        && ref.kind === "stable_ref"
        && (!Array.isArray(ref.identityPath) || ref.identityPath.length !== 2)) {
      errors.push(`${String(value.kind ?? "edge")}.${field} requires NodeGuid/PinId.`);
    }
  }

  for (const child of Object.values(value)) {
    validatePinOperationRefs(child, errors);
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
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
