import type { SourceSpan } from "../index.js";

export interface ParsedLine {
  text: string;
  line: number;
}

export class ParseError extends Error {
  constructor(
    readonly code: string,
    message: string,
    readonly span: SourceSpan,
  ) {
    super(message);
  }
}

export function preprocessLines(text: string): ParsedLine[] {
  const result: ParsedLine[] = [];
  const rawLines = text.split(/\r?\n/);

  for (let index = 0; index < rawLines.length; index += 1) {
    const trimmed = rawLines[index].trim();
    if (trimmed === "" || trimmed.startsWith("#")) {
      continue;
    }
    if (trimmed === "---") {
      break;
    }
    result.push({ text: trimmed, line: index + 1 });
  }

  return result;
}

export function spanForLine(line: ParsedLine): SourceSpan {
  return { line: line.line, column: 1, length: line.text.length };
}

export function unwrap(text: string, open: string, close: string, line: ParsedLine): string {
  const trimmed = text.trim();
  if (!trimmed.startsWith(open) || !trimmed.endsWith(close)) {
    throw new ParseError("invalid_wrapped_value", `Expected ${open}...${close}.`, spanForLine(line));
  }
  return trimmed.slice(open.length, -close.length);
}

export function splitTopLevel(text: string, separator: string): string[] {
  const parts: string[] = [];
  let depth = 0;
  let inString = false;
  let start = 0;

  for (let index = 0; index < text.length; index += 1) {
    const char = text[index];
    const prev = text[index - 1];
    if (char === '"' && prev !== "\\") {
      inString = !inString;
      continue;
    }
    if (inString) {
      continue;
    }
    if (char === "(" || char === "[" || char === "{") {
      depth += 1;
    } else if (char === ")" || char === "]" || char === "}") {
      depth -= 1;
    } else if (depth === 0 && text.slice(index, index + separator.length) === separator) {
      parts.push(text.slice(start, index).trim());
      start = index + separator.length;
      index += separator.length - 1;
    }
  }

  parts.push(text.slice(start).trim());
  return parts.filter((part) => part.length > 0);
}

export function findTopLevel(text: string, token: string): number {
  let depth = 0;
  let inString = false;

  for (let index = 0; index < text.length; index += 1) {
    const char = text[index];
    const prev = text[index - 1];
    if (char === '"' && prev !== "\\") {
      inString = !inString;
      continue;
    }
    if (inString) {
      continue;
    }
    if (char === "(" || char === "[" || char === "{") {
      depth += 1;
    } else if (char === ")" || char === "]" || char === "}") {
      depth -= 1;
    } else if (depth === 0 && text.slice(index, index + token.length) === token) {
      return index;
    }
  }

  return -1;
}
