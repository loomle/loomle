import { formatObjectText, formatTargetExpression } from "./formatter.js";
import { validateObjectResult } from "./schema-validator.js";
import type { Diagnostic, ObjectResult, TextResult } from "./index.js";

/**
 * Validates a normalized executor/RPC result and converts its ordered Object
 * Text into the public text envelope used by SAL-facing clients.
 */
export async function objectResultToTextResult(result: unknown): Promise<TextResult> {
  const resultDiagnostic = await validateObjectResult(result);
  if (resultDiagnostic) {
    return unresolvedTextResult([resultDiagnostic]);
  }

  return formatValidatedObjectResult(result as ObjectResult);
}

export function unresolvedTextResult(
  diagnostics: Diagnostic[],
): TextResult {
  return {
    targetContext: "unresolved_target",
    text: "result unresolved_target\nno_objects",
    diagnostics,
    isError: diagnostics.some((entry) => entry.severity === "error"),
  };
}

/** @internal The caller must have validated the ObjectResult first. */
export function formatValidatedObjectResult(result: ObjectResult): TextResult {
  const { object, ...fields } = result;
  return {
    ...fields,
    text: formatObjectResultText(result),
  };
}

export function formatObjectResultText(result: ObjectResult): string {
  const lines = [`result ${result.targetContext}`];
  if (result.targetContext !== "unresolved_target") {
    lines.push(`target ${result.target.alias} = ${formatTargetExpression(result.target.target)}`);
    for (const related of result.relatedTargets ?? []) {
      lines.push(`related ${related.alias} = ${formatTargetExpression(related.target)}`);
    }
    for (const handoff of result.handoffs ?? []) {
      lines.push(`handoff ${formatHandoffPurpose(handoff.purpose)} to ${handoff.target.name}`);
    }
  }
  if (result.object) {
    lines.push("objects");
    const objectText = formatObjectText(result.object);
    if (objectText) lines.push(objectText);
  } else {
    lines.push("no_objects");
  }
  return lines.join("\n");
}

function formatHandoffPurpose(purpose: string): string {
  return /^[A-Za-z_][A-Za-z0-9_]*$/.test(purpose)
    ? purpose
    : JSON.stringify(purpose);
}
