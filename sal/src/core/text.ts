import type { SourceSpan } from "../index.js";

export interface ParsedLine {
  kind: "code" | "comment";
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
  const stack: string[] = [];
  let buffer: string[] = [];
  let startLine = 1;

  for (let index = 0; index < rawLines.length; index += 1) {
    const raw = rawLines[index];
    const trimmed = raw.trim();

    if (stack.length === 0 && trimmed === "###") {
      if (buffer.length > 0) {
        throw lineError("language.comment_inside_statement", "A comment must start between statements.", index + 1, raw);
      }
      const commentLine = index + 1;
      const content: string[] = [];
      index += 1;
      while (index < rawLines.length && rawLines[index].trim() !== "###") {
        content.push(rawLines[index]);
        index += 1;
      }
      if (index >= rawLines.length) {
        throw lineError("language.unclosed_comment", "Unclosed multi-line comment.", commentLine, "###");
      }
      const comment = content.join("\n");
      if (comment.length === 0) {
        throw lineError("language.empty_comment", "A comment cannot be empty.", commentLine, "###");
      }
      result.push({ kind: "comment", text: comment, line: commentLine });
      continue;
    }

    if (stack.length === 0 && trimmed.startsWith("#")) {
      if (buffer.length > 0) {
        throw lineError("language.comment_inside_statement", "A comment must start between statements.", index + 1, raw);
      }
      const comment = trimmed.slice(1).trimStart();
      if (comment.length === 0) {
        throw lineError("language.empty_comment", "A comment cannot be empty.", index + 1, raw);
      }
      result.push({ kind: "comment", text: comment, line: index + 1 });
      continue;
    }

    if (trimmed === "" && stack.length === 0) {
      continue;
    }

    if (buffer.length === 0) {
      startLine = index + 1;
    }
    if (trimmed !== "") {
      buffer.push(trimmed);
    }
    updateDelimiterStack(raw, stack, index + 1);

    if (stack.length === 0 && buffer.length > 0) {
      result.push({ kind: "code", text: buffer.join(" "), line: startLine });
      buffer = [];
    }
  }

  if (stack.length > 0) {
    throw lineError("language.unclosed_delimiter", `Unclosed delimiter ${stack[stack.length - 1]}.`, startLine, buffer.join(" "));
  }
  return result;
}

function updateDelimiterStack(text: string, stack: string[], line: number): void {
  let inString = false;
  let escaped = false;
  for (const char of text) {
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
      continue;
    }
    if (char === "(" || char === "[" || char === "{") {
      stack.push(char);
      continue;
    }
    if (char === ")" || char === "]" || char === "}") {
      const expected = char === ")" ? "(" : char === "]" ? "[" : "{";
      if (stack.pop() !== expected) {
        throw lineError("language.unbalanced_delimiter", `Unexpected closing delimiter ${char}.`, line, text);
      }
    }
  }
  if (inString) {
    throw lineError("language.unclosed_string", "Quoted strings cannot continue across lines.", line, text);
  }
}

function lineError(code: string, message: string, line: number, text: string): ParseError {
  return new ParseError(code, message, { line, column: 1, length: text.length });
}

export function spanForLine(line: ParsedLine): SourceSpan {
  return { line: line.line, column: 1, length: line.text.length };
}

export function unwrap(text: string, open: string, close: string, line: ParsedLine): string {
  const trimmed = text.trim();
  if (!trimmed.startsWith(open) || !trimmed.endsWith(close)) {
    throw new ParseError("language.invalid_wrapped_value", `Expected ${open}...${close}.`, spanForLine(line));
  }
  return trimmed.slice(open.length, -close.length);
}

export function splitTopLevel(text: string, separator: string): string[] {
  return splitTopLevelExact(text, separator).filter((part) => part.length > 0);
}

export function splitTopLevelExact(text: string, separator: string): string[] {
  const parts: string[] = [];
  let depth = 0;
  let inString = false;
  let escaped = false;
  let start = 0;

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
  return parts;
}

export function findTopLevel(text: string, token: string): number {
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
