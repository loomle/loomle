import { formatSalObject } from "./formatter.js";
import { validateObjectResult } from "./schema-validator.js";
import type { ObjectResult, TextResult } from "./index.js";

/**
 * Validates a normalized executor/RPC result and converts its ordered Object
 * Text into the public text envelope used by SAL-facing clients.
 */
export async function objectResultToTextResult(result: unknown): Promise<TextResult> {
  const resultDiagnostic = await validateObjectResult(result);
  if (resultDiagnostic) {
    return { diagnostics: [resultDiagnostic] };
  }

  return formatValidatedObjectResult(result as ObjectResult);
}

/** @internal The caller must have validated the ObjectResult first. */
export function formatValidatedObjectResult(result: ObjectResult): TextResult {
  const { object, ...fields } = result;
  return {
    ...fields,
    ...(object ? { text: formatSalObject(object) } : {}),
  };
}
