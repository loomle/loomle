import type { Condition, Detail, Query } from "../index.js";
import { formatExpr, parseExpr } from "./expr.js";
import { ParseError, type ParsedLine, spanForLine, splitTopLevel } from "./text.js";

export function parseCondition(text: string, line: ParsedLine): Condition {
  return parseOrCondition(text.trim(), line);
}

function parseOrCondition(text: string, line: ParsedLine): Condition {
  const parts = splitByKeyword(text, "or");
  if (parts.length > 1) {
    return { kind: "or", conditions: parts.map((part) => parseAndCondition(part, line)) as [Condition, ...Condition[]] };
  }
  return parseAndCondition(text, line);
}

function parseAndCondition(text: string, line: ParsedLine): Condition {
  const parts = splitByKeyword(text, "and");
  if (parts.length > 1) {
    return { kind: "and", conditions: parts.map((part) => parseNotCondition(part, line)) as [Condition, ...Condition[]] };
  }
  return parseNotCondition(text, line);
}

function parseNotCondition(text: string, line: ParsedLine): Condition {
  const trimmed = trimParens(text.trim());
  if (trimmed.startsWith("not ")) {
    return { kind: "not", condition: parseCondition(trimmed.slice("not ".length), line) };
  }
  return parseComparisonCondition(trimmed, line);
}

function parseComparisonCondition(text: string, line: ParsedLine): Condition {
  const match = /^([A-Za-z_][A-Za-z0-9_.]*)\s*(~=|!=|>=|<=|>|<|=)\s*(.+)$/.exec(text);
  if (!match) {
    throw new ParseError("unsupported_condition", "Unsupported condition.", spanForLine(line));
  }
  const field = { path: match[1].split(".") as [string, ...string[]] };
  const value = parseExpr(match[3], line);
  switch (match[2]) {
    case "=":
      return { kind: "eq", field, value };
    case "!=":
      return { kind: "ne", field, value };
    case "~=":
      return { kind: "contains", field, value };
    case ">":
      return { kind: "compare", op: "gt", field, value };
    case ">=":
      return { kind: "compare", op: "gte", field, value };
    case "<":
      return { kind: "compare", op: "lt", field, value };
    case "<=":
      return { kind: "compare", op: "lte", field, value };
    default:
      return assertNever(match[2] as never);
  }
}

function splitByKeyword(text: string, keyword: "and" | "or"): string[] {
  return splitTopLevel(text, " ")
    .reduce<string[]>((parts, token) => {
      if (token === keyword) {
        parts.push("");
      } else {
        parts[parts.length - 1] = parts[parts.length - 1]
          ? `${parts[parts.length - 1]} ${token}`
          : token;
      }
      return parts;
    }, [""])
    .map((part) => part.trim())
    .filter(Boolean);
}

function trimParens(text: string): string {
  return text.startsWith("(") && text.endsWith(")") ? text.slice(1, -1).trim() : text;
}

export function parseDetails(text: string, line: ParsedLine): Detail[] {
  const details = text.split(",").map((part) => part.trim());
  if (details.length === 0 || details.some((detail) => detail.length === 0)) {
    throw new ParseError("language.invalid_detail", "Expected with <detail>, <detail>.", spanForLine(line));
  }
  const allowed = new Set(["pins", "defaults", "properties", "registryTags"]);
  const invalid = details.find((detail) => !allowed.has(detail));
  if (invalid) {
    throw new ParseError("language.invalid_detail", `Unsupported detail: ${invalid}.`, spanForLine(line));
  }
  return details as Detail[];
}

export function parseOrderBy(text: string, line: ParsedLine): Query["orderBy"] {
  const parts = text.split(",").map((part) => part.trim());
  if (parts.length === 0 || parts.some((part) => part.length === 0)) {
    throw new ParseError("language.invalid_order_by", "Expected order by <key> [asc|desc].", spanForLine(line));
  }
  return parts.map((part) => {
    const match = /^([A-Za-z_][A-Za-z0-9_.]*)(?:\s+(asc|desc))?$/.exec(part);
    if (!match) {
      throw new ParseError("language.invalid_order_by", "Expected order by <key> [asc|desc].", spanForLine(line));
    }
    return { key: match[1], direction: (match[2] as "asc" | "desc" | undefined) ?? "asc" };
  });
}

export function parsePage(line: ParsedLine): Query["page"] {
  let match = /^page\s+limit\s+(\d+)$/.exec(line.text);
  if (match) {
    const limit = Number(match[1]);
    if (limit < 1) {
      throw new ParseError("language.invalid_page", "Page limit must be greater than zero.", spanForLine(line));
    }
    return { limit };
  }
  match = /^page\s+after\s+"([^"]+)"$/.exec(line.text);
  if (match) {
    return { after: match[1] };
  }
  throw new ParseError("language.invalid_page", "Expected page limit <number> or page after \"cursor\".", spanForLine(line));
}

export function formatCondition(condition: Condition): string {
  switch (condition.kind) {
    case "eq":
      return `${formatFieldPath(condition.field.path)} = ${formatExpr(condition.value)}`;
    case "ne":
      return `${formatFieldPath(condition.field.path)} != ${formatExpr(condition.value)}`;
    case "contains":
      return `${formatFieldPath(condition.field.path)} ~= ${formatExpr(condition.value)}`;
    case "compare":
      return `${formatFieldPath(condition.field.path)} ${formatCompareOp(condition.op)} ${formatExpr(condition.value)}`;
    case "not":
      return `not ${formatConditionAtom(condition.condition)}`;
    case "and":
      return condition.conditions.map(formatConditionAtom).join(" and ");
    case "or":
      return condition.conditions.map(formatConditionAtom).join(" or ");
    default:
      return assertNever(condition);
  }
}

function formatConditionAtom(condition: Condition): string {
  return condition.kind === "and" || condition.kind === "or"
    ? `(${formatCondition(condition)})`
    : formatCondition(condition);
}

function formatCompareOp(op: "gt" | "gte" | "lt" | "lte"): string {
  switch (op) {
    case "gt":
      return ">";
    case "gte":
      return ">=";
    case "lt":
      return "<";
    case "lte":
      return "<=";
    default:
      return assertNever(op);
  }
}

function formatFieldPath(path: string[]): string {
  return path.join(".");
}

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}
