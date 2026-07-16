import type {
  Add,
  Binding,
  Expr,
  Move,
  ObjectText,
  Patch,
  PatchOperation,
  Query,
  CollectionOperation,
  QueryOperation,
  Ref,
  SalObject,
  Target,
} from "./index.js";
import { formatBinding } from "./core/binding.js";
import { formatCondition } from "./core/condition.js";
import { formatArgList, formatExpr, formatRef } from "./core/expr.js";

export function formatSalObject(object: SalObject): string {
  if (isQuery(object)) return formatQuery(object);
  if (isPatch(object)) return formatPatch(object);
  return formatObjectText(object);
}

function formatObjectText(object: ObjectText): string {
  return object.statements.map((statement) => {
    if ("target" in statement && "value" in statement) return formatBinding(statement);
    if ("from" in statement) return `${formatRef(statement.from)} -> ${formatRef(statement.to)}`;
    return statement.text.includes("\n") || /^\s|\s$/.test(statement.text)
      ? `###\n${statement.text}\n###`
      : `# ${statement.text}`;
  }).join("\n");
}

function formatQuery(query: Query): string {
  const lines = [...formatTarget(query.target), `query ${query.target.alias}`, formatQueryOperation(query.operation)];
  if (query.where) lines.push(`where ${formatCondition(query.where)}`);
  if (query.with) lines.push(`with ${query.with.join(", ")}`);
  if (query.orderBy) lines.push(`order by ${query.orderBy.map((item) => `${item.key} ${item.direction}`).join(", ")}`);
  if (query.page?.limit !== undefined) lines.push(`page limit ${query.page.limit}`);
  if (query.page?.after !== undefined) lines.push(`page after ${JSON.stringify(query.page.after)}`);
  return lines.join("\n");
}

function formatQueryOperation(operation: QueryOperation): string {
  if (operation.kind === "summary") return "summary";
  if (operation.kind === "exec_flow" || operation.kind === "data_flow") {
    return `${operation.kind === "exec_flow" ? "exec flow" : "data flow"} ${operation.direction} ${formatRef(operation.target)}${formatDepth(operation.depth)}`;
  }
  if (operation.kind === "context") return `context ${formatRef(operation.target)}${formatDepth(operation.depth)}`;
  if (operation.kind === "tree") {
    return `tree${operation.root ? ` ${formatRef(operation.root)}` : ""}${formatDepth(operation.depth)}`;
  }
  if (operation.kind === "palette_entries") {
    const text = operation.text ? ` ${JSON.stringify(operation.text)}` : "";
    const context = operation.pinContext ? ` ${operation.pinContext.direction} ${formatRef(operation.pinContext.pin)}` : "";
    return `palette entries${text}${context}`;
  }
  if (isCollectionOperation(operation)) return `${operation.kind}${operation.text ? ` ${JSON.stringify(operation.text)}` : ""}`;
  if ("name" in operation) return `${operation.kind} ${formatExactName(operation.name)}`;
  if ("id" in operation) return operation.kind === "palette" ? `palette @${operation.id}` : `${operation.kind}@${operation.id}`;
  throw new Error(`Unsupported Query operation ${operation.kind}.`);
}

function formatPatch(patch: Patch): string {
  const lines = [...formatTarget(patch.target), `patch ${patch.target.alias}${patch.dryRun ? " dry run" : ""}`];
  for (const statement of patch.statements) {
    lines.push(isBinding(statement) ? formatBinding(statement) : formatPatchOperation(statement));
  }
  return lines.join("\n");
}

function formatTarget(target: Target): string[] {
  return target.value.kind === "name" && target.value.name === target.alias
    ? []
    : [`${target.alias} = ${formatExpr(target.value)}`, ""];
}

function formatPatchOperation(operation: PatchOperation): string {
  switch (operation.kind) {
    case "add": return formatAdd(operation);
    case "remove": return `remove ${formatRef(operation.target)}`;
    case "set": return `set ${formatRef(operation.target)} = ${formatExpr(operation.value)}`;
    case "reset": return `reset ${formatRef(operation.target)}`;
    case "move": return formatMove(operation);
    case "connect": return `connect ${formatRef(operation.from)} -> ${formatRef(operation.to)}`;
    case "disconnect": return `disconnect ${formatRef(operation.from)} -> ${formatRef(operation.to)}`;
    case "break": return `break ${formatRef(operation.target)}`;
    case "insert": return `insert ${formatRef(operation.from)} -> ${formatRef(operation.input)}/${formatRef(operation.output)} -> ${formatRef(operation.to)}`;
    case "wrap": {
      const targets = operation.targets.length === 1
        ? formatRef(operation.targets[0])
        : `[${operation.targets.map(formatRef).join(", ")}]`;
      return `wrap ${targets} with ${formatRef(operation.with)}`;
    }
    case "replace": return `replace ${formatRef(operation.target)} with ${formatRef(operation.with)}`;
    case "invoke": {
      const outputs = operation.outputs.length === 0 ? "" : ` as ${operation.outputs.map((output) => output.selector ? `${output.selector}: ${output.alias}` : output.alias).join(", ")}`;
      return `invoke ${formatRef(operation.target)} ${operation.operation}(${formatArgList(operation.args)})${outputs}`;
    }
    case "compile": return "compile";
    case "save": return "save";
  }
}

function formatAdd(operation: Add): string {
  const target = formatBindingTarget(operation.target);
  if (operation.to) return `add ${target} to ${formatRef(operation.to)}`;
  if (operation.before) return `add ${target} before ${formatRef(operation.before)}`;
  if (operation.after) return `add ${target} after ${formatRef(operation.after)}`;
  return `add ${target}`;
}

function formatMove(operation: Move): string {
  const target = formatRef(operation.target);
  if (operation.to) return `move ${target} to ${formatDestination(operation.to)}`;
  if (operation.by) return `move ${target} by (${operation.by.join(", ")})`;
  if (operation.before) return `move ${target} before ${formatRef(operation.before)}`;
  if (operation.after) return `move ${target} after ${formatRef(operation.after)}`;
  throw new Error("Move has no destination.");
}

function formatDestination(value: Ref | [unknown, unknown]): string {
  return Array.isArray(value) ? `(${value.join(", ")})` : formatRef(value);
}

function formatBindingTarget(target: Binding["target"]): string {
  return target.kind === "local" ? target.name : formatRef(target);
}

function formatDepth(depth: number | undefined): string {
  return depth === undefined ? "" : ` depth ${depth}`;
}

function formatExactName(name: string): string {
  return /^\S+$/.test(name) ? name : JSON.stringify(name);
}

function isCollectionOperation(operation: QueryOperation): operation is CollectionOperation {
  return ["assets", "variables", "dispatchers", "graphs", "components", "nodes", "properties", "functions", "defaults", "widgets"].includes(operation.kind);
}

function isBinding(value: Patch["statements"][number]): value is Binding {
  return "target" in value && "value" in value && !("kind" in value);
}

function isQuery(object: SalObject): object is Query {
  return "kind" in object && object.kind === "query";
}

function isPatch(object: SalObject): object is Patch {
  return "kind" in object && object.kind === "patch";
}
