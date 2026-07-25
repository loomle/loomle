import { loadInterfaceSchema, selectActiveInterfaces } from "./interface-schema.js";
import { parseSalObject } from "./parser.js";
import { formatValidatedObjectResult, unresolvedTextResult } from "./result.js";
import { validateSalObject, validateObjectResult } from "./schema-validator.js";
import type {
  CreateSalOptions,
  Diagnostic,
  Sal,
  SalExecutionOptions,
  SalExecutor,
  CanonicalTargetBinding,
  ExactQueryResult,
  ObjectText,
  ObjectResult,
  Patch,
  Query,
  TextResult,
} from "./index.js";

export function createSal(options: CreateSalOptions): Sal {
  const executor = options.executor;
  const interfaces = selectActiveInterfaces(options.catalog, executor.interfaces);

  return {
    query(text, executionOptions) {
      return run("query", text, executor, executionOptions);
    },
    patch(text, executionOptions) {
      return run("patch", text, executor, executionOptions);
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
  executionOptions?: SalExecutionOptions,
): Promise<TextResult> {
  const parsed = parseSalObject(text);
  if (!parsed.object) {
    return unresolvedTextResult(parsed.diagnostics);
  }

  const objectDiagnostic = await validateSalObject(parsed.object);
  if (objectDiagnostic) {
    return unresolvedTextResult([objectDiagnostic]);
  }

  if (!("kind" in parsed.object) || parsed.object.kind !== expectedKind) {
    return unresolvedTextResult([
        {
          severity: "error",
          code: "language.wrong_document_kind",
          message: `Expected ${expectedKind} Text but received ${"kind" in parsed.object ? parsed.object.kind : "Object"} Text.`,
        },
      ]);
  }

  const patchExecutor = executor.patch;
  if (expectedKind === "patch" && !patchExecutor) {
    return unresolvedTextResult([
        {
          severity: "error",
          code: "capability.patch_unavailable",
          message: "The configured SAL executor does not support Patch requests.",
        },
      ]);
  }

  const result = expectedKind === "query"
    ? await executor.query(parsed.object as Query, executionOptions)
    : await patchExecutor!(parsed.object as Patch, executionOptions);

  const resultDiagnostic = await validateObjectResult(result);
  if (resultDiagnostic) {
    return unresolvedTextResult([resultDiagnostic]);
  }
  const isMutationResult = "isError" in result;
  if ((expectedKind === "patch") !== isMutationResult) {
    return unresolvedTextResult([
        diagnostic(
          "language.invalid_result_shape",
          expectedKind === "patch"
            ? "Patch executor must return MutationResult execution fields."
            : "Query executor must return Result without mutation execution fields.",
        ),
      ]);
  }

  return formatValidatedObjectResult(result);
}

export function diagnostic(
  code: string,
  message: string,
  severity: Diagnostic["severity"] = "error",
): Diagnostic {
  return { severity, code, message };
}

export function echoObjectResult(
  target: CanonicalTargetBinding,
  object: ObjectText,
): ExactQueryResult {
  return { targetContext: "exact_target", target, object, diagnostics: [] };
}
