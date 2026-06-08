import type { BindingContext, BindingSource, BindingWhere, Document, Literal, PinRef, UseBinding } from "./ir.js";

export function printLgl(document: Document): string {
  const lines: string[] = [];
  for (const binding of document.bindings) {
    lines.push(printUseBinding(binding));
  }
  if (lines.length > 0) {
    lines.push("");
  }
  lines.push(`${document.kind} ${document.name}`, "");
  let previousKind: string | undefined;
  for (const statement of document.statements) {
    if (document.kind === "graph" && previousKind === "node" && statement.kind === "edge") {
      lines.push("");
    }
    switch (statement.kind) {
      case "node":
        lines.push(`${statement.alias} = ${statement.type}(${statement.args.map(printLiteral).join(", ")})`);
        break;
      case "edge":
        lines.push(`${printPinRef(statement.from)} -> ${printPinRef(statement.to)}`);
        break;
      case "set":
        lines.push(`set ${statement.target.node}.${statement.target.property} = ${printLiteral(statement.value)}`);
        break;
      case "add":
        lines.push(`add ${statement.node.alias} = ${statement.node.type}(${statement.node.args.map(printLiteral).join(", ")})`);
        break;
      case "rewire":
        lines.push(`rewire ${printPinRef(statement.from)} -> ${printPinRef(statement.to)}`);
        break;
      case "connect":
        lines.push(`${printPinRef(statement.from)} -> ${printPinRef(statement.to)}`);
        break;
    }
    previousKind = statement.kind;
  }
  return `${lines.join("\n")}\n`;
}

function printUseBinding(binding: UseBinding): string {
  const clauses = [`use ${binding.symbol} from ${printBindingSource(binding.source)}`];
  if (binding.context) {
    clauses.push(printBindingContext(binding.context));
  }
  for (const where of binding.where ?? []) {
    clauses.push(printBindingWhere(where));
  }
  return clauses.join(" ");
}

function printBindingSource(source: BindingSource): string {
  if (source.kind === "palette") {
    return `palette ${JSON.stringify(source.query)}`;
  }
  return `palette entry ${JSON.stringify(source.id)}`;
}

function printBindingContext(context: BindingContext): string {
  if (context.kind === "component") {
    return `context component ${JSON.stringify(context.name)}`;
  }
  return `context from ${printPinRef(context.pin)}`;
}

function printBindingWhere(where: BindingWhere): string {
  return `where ${where.key} = ${printLiteral(where.value)}`;
}

function printPinRef(ref: PinRef): string {
  return `${ref.node}.${ref.pin}`;
}

function printLiteral(literal: Literal): string {
  switch (literal.kind) {
    case "string":
      return JSON.stringify(literal.value);
    case "number":
      return String(literal.value);
    case "identifier":
      return literal.value;
    case "boolean":
      return literal.value ? "true" : "false";
    case "null":
      return "null";
  }
}
