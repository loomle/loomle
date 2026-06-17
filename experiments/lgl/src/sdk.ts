import { formatLglObject } from "./formatter.js";
import { parseLglObject } from "./parser.js";
import type {
  Adapter,
  CreateLglOptions,
  Diagnostic,
  Lgl,
  LglObject,
  ObjectResult,
  Patch,
  Query,
  TextResult,
} from "./index.js";

export function createLgl(options: CreateLglOptions = {}): Lgl {
  const adapters = new Map<string, Adapter>();
  for (const adapter of options.adapters ?? []) {
    adapters.set(adapter.domain, adapter);
  }

  return {
    query(text) {
      return run("query", text, adapters);
    },
    patch(text) {
      return run("patch", text, adapters);
    },
  };
}

async function run(
  expectedKind: "query" | "patch",
  text: string,
  adapters: Map<string, Adapter>,
): Promise<TextResult> {
  const parsed = parseLglObject(text);
  if (!parsed.object) {
    return { diagnostics: parsed.diagnostics };
  }

  if (parsed.object.kind !== expectedKind) {
    return {
      diagnostics: [
        {
          severity: "error",
          code: "wrong_document_kind",
          message: `Expected a ${expectedKind} document but received ${parsed.object.kind}.`,
        },
      ],
    };
  }

  const adapter = adapters.get(parsed.object.target.domain);
  if (!adapter) {
    return {
      diagnostics: [
        {
          severity: "error",
          code: "missing_adapter",
          message: `No LGL adapter is registered for domain ${parsed.object.target.domain}.`,
          suggestion: `Register an adapter with domain ${parsed.object.target.domain}.`,
        },
      ],
    };
  }

  const result =
    expectedKind === "query"
      ? await adapter.query(parsed.object as Query)
      : await adapter.patch(parsed.object as Patch);

  return objectResultToTextResult(result);
}

function objectResultToTextResult(result: ObjectResult): TextResult {
  return {
    ...(result.object ? { text: formatLglObject(result.object) } : {}),
    diagnostics: result.diagnostics,
  };
}

export function diagnostic(
  code: string,
  message: string,
  severity: Diagnostic["severity"] = "error",
): Diagnostic {
  return { severity, code, message };
}

export function echoObjectResult(object: LglObject): ObjectResult {
  return { object, diagnostics: [] };
}
