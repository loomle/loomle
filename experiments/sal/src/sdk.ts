import { formatSalObject } from "./formatter.js";
import { loadInterfaceSchema, normalizeInterfaceModules } from "./interface-schema.js";
import { parseSalObject } from "./parser.js";
import { validateSalObject, validateObjectResult } from "./schema-validator.js";
import type {
  CreateSalOptions,
  Diagnostic,
  Sal,
  SalExecutor,
  ObjectText,
  ObjectResult,
  Patch,
  Query,
  TextResult,
} from "./index.js";

export function createSal(options: CreateSalOptions): Sal {
  const executor = options.executor;
  const interfaces = normalizeInterfaceModules(executor.interfaces);

  return {
    query(text) {
      return run("query", text, executor);
    },
    patch(text) {
      return run("patch", text, executor);
    },
    schema(module) {
      return loadInterfaceSchema(interfaces, module);
    },
  };
}

async function run(
  expectedKind: "query" | "patch",
  text: string,
  executor: SalExecutor,
): Promise<TextResult> {
  const parsed = parseSalObject(text);
  if (!parsed.object) {
    return { diagnostics: parsed.diagnostics };
  }

  const objectDiagnostic = await validateSalObject(parsed.object);
  if (objectDiagnostic) {
    return { diagnostics: [objectDiagnostic] };
  }

  if (!("kind" in parsed.object) || parsed.object.kind !== expectedKind) {
    return {
      diagnostics: [
        {
          severity: "error",
          code: "language.wrong_document_kind",
          message: `Expected ${expectedKind} Text but received ${"kind" in parsed.object ? parsed.object.kind : "Object"} Text.`,
        },
      ],
    };
  }

  const patchExecutor = executor.patch;
  if (expectedKind === "patch" && !patchExecutor) {
    return {
      diagnostics: [
        {
          severity: "error",
          code: "capability.patch_unavailable",
          message: "The configured SAL executor does not support Patch requests.",
        },
      ],
    };
  }

  const result = expectedKind === "query"
    ? await executor.query(parsed.object as Query)
    : await patchExecutor!(parsed.object as Patch);

  const resultDiagnostic = await validateObjectResult(result);
  if (resultDiagnostic) {
    return { diagnostics: [resultDiagnostic] };
  }
  const isMutationResult = "isError" in result;
  if ((expectedKind === "patch") !== isMutationResult) {
    return {
      diagnostics: [
        diagnostic(
          "language.invalid_result_shape",
          expectedKind === "patch"
            ? "Patch executor must return MutationResult execution fields."
            : "Query executor must return Result without mutation execution fields.",
        ),
      ],
    };
  }

  return objectResultToTextResult(result);
}

function objectResultToTextResult(result: ObjectResult): TextResult {
  const { object, ...fields } = result;
  return {
    ...fields,
    ...(object ? { text: formatSalObject(object) } : {}),
  };
}

export function diagnostic(
  code: string,
  message: string,
  severity: Diagnostic["severity"] = "error",
): Diagnostic {
  return { severity, code, message };
}

export function echoObjectResult(object: ObjectText): ObjectResult {
  return { object, diagnostics: [] };
}
