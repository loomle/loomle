import type { Condition, OrderBy, Page } from "../index.js";
import { formatExpr, parseExpr } from "./expr.js";
import { ParseError, type ParsedLine, spanForLine, splitTopLevel } from "./text.js";

export function parseCondition(
  text: string,
  line: ParsedLine,
  aliases: ReadonlySet<string> = new Set(),
): Condition {
  return parseOrCondition(text.trim(), line, aliases);
}

function parseOrCondition(text: string, line: ParsedLine, aliases: ReadonlySet<string>): Condition {
  const parts = splitByKeyword(text, "or");
  if (parts.length > 1) {
    return { kind: "or", conditions: parts.map((part) => parseAndCondition(part, line, aliases)) as [Condition, Condition, ...Condition[]] };
  }
  return parseAndCondition(text, line, aliases);
}

function parseAndCondition(text: string, line: ParsedLine, aliases: ReadonlySet<string>): Condition {
  const parts = splitByKeyword(text, "and");
  if (parts.length > 1) {
    return { kind: "and", conditions: parts.map((part) => parseNotCondition(part, line, aliases)) as [Condition, Condition, ...Condition[]] };
  }
  return parseNotCondition(text, line, aliases);
}

function parseNotCondition(text: string, line: ParsedLine, aliases: ReadonlySet<string>): Condition {
  const trimmed = text.trim();
  const grouped = unwrapOuterParens(trimmed);
  if (grouped !== undefined) {
    return parseCondition(grouped, line, aliases);
  }
  if (trimmed.startsWith("not ")) {
    return { kind: "not", condition: parseCondition(trimmed.slice(4), line, aliases) };
  }
  return parseComparisonCondition(trimmed, line, aliases);
}

function parseComparisonCondition(text: string, line: ParsedLine, aliases: ReadonlySet<string>): Condition {
  const match = /^([A-Za-z_][A-Za-z0-9_.]*)\s*(~=|!=|>=|<=|>|<|=)\s*(.+)$/.exec(text);
  if (!match) {
    if (/^[A-Za-z_][A-Za-z0-9_.]*$/.test(text)) {
      return { kind: "eq", field: { path: text.split(".") as [string, ...string[]] }, value: true };
    }
    throw new ParseError("language.unsupported_condition", "Unsupported condition.", spanForLine(line));
  }
  const field = { path: match[1].split(".") as [string, ...string[]] };
  const value = parseExpr(match[3], line, aliases);
  switch (match[2]) {
    case "=": return { kind: "eq", field, value };
    case "!=": return { kind: "ne", field, value };
    case "~=": return { kind: "contains", field, value };
    case ">": return { kind: "compare", op: "gt", field, value };
    case ">=": return { kind: "compare", op: "gte", field, value };
    case "<": return { kind: "compare", op: "lt", field, value };
    case "<=": return { kind: "compare", op: "lte", field, value };
    default: throw new Error(`Unexpected condition operator ${match[2]}.`);
  }
}

function splitByKeyword(text: string, keyword: "and" | "or"): string[] {
  return splitTopLevel(text, " ").reduce<string[]>((parts, token) => {
    if (token === keyword) {
      parts.push("");
    } else {
      parts[parts.length - 1] = parts[parts.length - 1] ? `${parts[parts.length - 1]} ${token}` : token;
    }
    return parts;
  }, [""]).map((part) => part.trim()).filter(Boolean);
}

function unwrapOuterParens(text: string): string | undefined {
  if (!text.startsWith("(") || !text.endsWith(")")) {
    return undefined;
  }
  let depth = 0;
  let inString = false;
  let escaped = false;
  for (let index = 0; index < text.length; index += 1) {
    const char = text[index];
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (char === "\\") {
        escaped = true;
      } else if (char === '"') {
        inString = false;
      }
      continue;
    }
    if (char === '"') {
      inString = true;
    } else if (char === "(") {
      depth += 1;
    } else if (char === ")") {
      depth -= 1;
      if (depth === 0 && index < text.length - 1) {
        return undefined;
      }
    }
  }
  return depth === 0 ? text.slice(1, -1).trim() : undefined;
}

export function parseDetails(text: string, line: ParsedLine): string[] {
  const details = text.split(",").map((part) => part.trim());
  if (details.length === 0 || details.some((detail) => !/^[A-Za-z_][A-Za-z0-9_]*$/.test(detail))) {
    throw new ParseError("language.invalid_detail", "Expected with <detail>, <detail>.", spanForLine(line));
  }
  return details;
}

export function parseOrderBy(text: string, line: ParsedLine): OrderBy[] {
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

export function parsePage(line: ParsedLine): Page {
  let match = /^page\s+limit\s+(\d+)$/.exec(line.text);
  if (match) {
    const limit = Number(match[1]);
    if (limit < 1) {
      throw new ParseError("language.invalid_page", "Page limit must be greater than zero.", spanForLine(line));
    }
    return { limit };
  }
  match = /^page\s+after\s+("(?:[^"\\]|\\.)*")$/.exec(line.text);
  if (match) {
    try {
      return { after: JSON.parse(match[1]) as string };
    } catch {
      throw new ParseError("language.invalid_string", "Invalid quoted cursor.", spanForLine(line));
    }
  }
  throw new ParseError("language.invalid_page", "Expected page limit <number> or page after \"cursor\".", spanForLine(line));
}

export function formatCondition(condition: Condition): string {
  switch (condition.kind) {
    case "eq": return condition.value === true ? formatFieldPath(condition.field.path) : `${formatFieldPath(condition.field.path)} = ${formatExpr(condition.value)}`;
    case "ne": return `${formatFieldPath(condition.field.path)} != ${formatExpr(condition.value)}`;
    case "contains": return `${formatFieldPath(condition.field.path)} ~= ${formatExpr(condition.value)}`;
    case "compare": return `${formatFieldPath(condition.field.path)} ${formatCompareOp(condition.op)} ${formatExpr(condition.value)}`;
    case "not": return `not ${formatConditionAtom(condition.condition)}`;
    case "and": return condition.conditions.map(formatConditionAtom).join(" and ");
    case "or": return condition.conditions.map(formatConditionAtom).join(" or ");
  }
}

function formatConditionAtom(condition: Condition): string {
  return condition.kind === "and" || condition.kind === "or" ? `(${formatCondition(condition)})` : formatCondition(condition);
}

function formatCompareOp(op: "gt" | "gte" | "lt" | "lte"): string {
  return ({ gt: ">", gte: ">=", lt: "<", lte: "<=" } as const)[op];
}

function formatFieldPath(path: string[]): string {
  return path.join(".");
}
