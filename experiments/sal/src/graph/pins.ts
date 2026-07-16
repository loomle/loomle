import type { Edge, PinRef } from "../index.js";
import { ParseError, type ParsedLine, spanForLine } from "../core/text.js";

export function parsePinChain(text: string, line: ParsedLine): PinRef[] {
  const parts = text.split("->").map((part) => part.trim());
  if (parts.length < 2) {
    throw new ParseError("invalid_pin_chain", "Pin chains require at least two segments.", spanForLine(line));
  }

  const pins: PinRef[] = [];
  for (const part of parts) {
    if (part.includes("/")) {
      const [input, output] = part.split("/").map((value) => value.trim());
      const inputRef = parsePinRef(input, line);
      pins.push(inputRef, parsePinRef(output.includes(".") ? output : `${inputRef.node}.${output}`, line));
    } else {
      pins.push(parsePinRef(part, line));
    }
  }
  return pins;
}

export function edgesFromChain(pins: PinRef[]): Edge[] {
  const edges: Edge[] = [];
  for (let index = 0; index < pins.length - 1; index += 2) {
    edges.push({ from: pins[index], to: pins[index + 1] });
  }
  return edges;
}

export function parsePinRef(text: string, line: ParsedLine): PinRef {
  const match = /^([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)$/.exec(text.trim());
  if (!match) {
    throw new ParseError("invalid_pin_ref", "Expected pin reference node.pin.", spanForLine(line));
  }
  return { node: match[1], pin: match[2] };
}
