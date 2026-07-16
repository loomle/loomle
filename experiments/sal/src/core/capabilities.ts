import type { Condition, Diagnostic, Query } from "../index.js";

export interface QueryCapabilities {
  domain: string;
  findKinds?: string[];
  whereFields?: string[];
  details?: string[];
  orderKeys?: string[];
  supportsPageAfter?: boolean;
  supportsCompare?: boolean;
}

export function validateQueryCapabilities(
  query: Query,
  capabilities: QueryCapabilities,
): Diagnostic[] {
  const diagnostics: Diagnostic[] = [];

  if (query.find && capabilities.findKinds && !capabilities.findKinds.includes(query.find.kind)) {
    diagnostics.push(capabilityDiagnostic(
      "capability.unsupported_find",
      `Domain ${capabilities.domain} does not support find ${query.find.kind}.`,
      capabilities,
      ["find", "kind"],
      query.find.kind,
      capabilities.findKinds,
    ));
  }

  for (const detail of query.with ?? []) {
    if (capabilities.details && !capabilities.details.includes(detail)) {
      diagnostics.push(capabilityDiagnostic(
        "capability.unsupported_detail",
        `Domain ${capabilities.domain} does not support detail ${detail}.`,
        capabilities,
        ["with"],
        detail,
        capabilities.details,
      ));
    }
  }

  for (const order of query.orderBy ?? []) {
    if (capabilities.orderKeys && !matchesField(order.key.split("."), capabilities.orderKeys)) {
      diagnostics.push(capabilityDiagnostic(
        "capability.unsupported_order_key",
        `Domain ${capabilities.domain} does not support order key ${order.key}.`,
        capabilities,
        ["orderBy"],
        order.key,
        capabilities.orderKeys,
      ));
    }
  }

  if (query.page?.after && !capabilities.supportsPageAfter) {
    diagnostics.push(capabilityDiagnostic(
      "capability.unsupported_pagination",
      `Domain ${capabilities.domain} does not support page after cursors.`,
      capabilities,
      ["page", "after"],
      "after",
      ["limit"],
    ));
  }

  collectConditionDiagnostics(query.where, capabilities, ["where"], diagnostics);
  return diagnostics;
}

function collectConditionDiagnostics(
  condition: Condition | undefined,
  capabilities: QueryCapabilities,
  path: Array<string | number>,
  diagnostics: Diagnostic[],
): void {
  if (!condition) {
    return;
  }

  switch (condition.kind) {
    case "eq":
    case "ne":
    case "contains":
    case "compare": {
      const field = condition.field.path.join(".");
      if (condition.kind === "compare" && !capabilities.supportsCompare) {
        diagnostics.push(capabilityDiagnostic(
          "capability.unsupported_compare",
          `Domain ${capabilities.domain} does not support ordered comparison filters.`,
          capabilities,
          [...path, "kind"],
          "compare",
          ["eq", "ne", "contains"],
        ));
      }
      if (capabilities.whereFields && !matchesField(condition.field.path, capabilities.whereFields)) {
        diagnostics.push(capabilityDiagnostic(
          "capability.unsupported_where_field",
          `Domain ${capabilities.domain} does not support where field ${field}.`,
          capabilities,
          [...path, "field"],
          field,
          capabilities.whereFields,
        ));
      }
      return;
    }
    case "not":
      collectConditionDiagnostics(condition.condition, capabilities, [...path, "condition"], diagnostics);
      return;
    case "and":
    case "or":
      condition.conditions.forEach((item, index) =>
        collectConditionDiagnostics(item, capabilities, [...path, "conditions", index], diagnostics),
      );
      return;
    default:
      assertNever(condition);
  }
}

function matchesField(path: string[], supported: string[]): boolean {
  const field = path.join(".");
  return supported.some((candidate) => {
    if (candidate === "*") {
      return true;
    }
    if (candidate.endsWith(".*")) {
      const prefix = candidate.slice(0, -2);
      return path[0] === prefix;
    }
    return candidate === field;
  });
}

function capabilityDiagnostic(
  code: string,
  message: string,
  capabilities: QueryCapabilities,
  path: Array<string | number>,
  actual: unknown,
  supported: unknown,
): Diagnostic {
  return {
    severity: "error",
    code,
    message,
    domain: capabilities.domain,
    path,
    actual,
    supported,
  };
}

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}
