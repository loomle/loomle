import type {
  Binding,
  BindingTarget,
  Call,
  Condition,
  Expr,
  LocalRef,
  MemberRef,
  MutationResult,
  ObjectResult,
  ObjectText,
  Patch,
  PatchOperation,
  Query,
  Ref,
  Result,
  SalExecutor,
  StableRef,
  Statement,
  Target,
} from "./index.js";

export interface MemoryDocument {
  target: Target;
  object: ObjectText;
}

export interface CreateMemoryExecutorOptions {
  interfaces: readonly string[];
  documents: MemoryDocument[];
}

export interface MemoryExecutor extends SalExecutor {
  getDocuments(): MemoryDocument[];
}

export interface CreateMemoryGraphExecutorOptions {
  documents: MemoryDocument[];
}

export type MemoryGraphExecutor = MemoryExecutor;

export function createMemoryGraphExecutor(options: CreateMemoryGraphExecutorOptions): MemoryGraphExecutor {
  return createMemoryExecutor({ interfaces: ["graph"], documents: options.documents });
}

export function createMemoryExecutor(options: CreateMemoryExecutorOptions): MemoryExecutor {
  let documents = structuredClone(options.documents);

  return {
    interfaces: [...options.interfaces],
    getDocuments() {
      return structuredClone(documents);
    },
    async query(query) {
      const document = findDocument(documents, query.target);
      if (!document) return notFound(query.target);
      return executeQuery(document.object, query);
    },
    async patch(patch) {
      const index = documents.findIndex((document) => sameTarget(document.target, patch.target));
      if (index < 0) {
        return mutationResult(patch, undefined, notFound(patch.target).diagnostics, false, false);
      }
      const planned = applyPatch(documents[index].object, patch);
      if ("diagnostics" in planned && planned.diagnostics.some((item) => item.severity === "error")) {
        return planned;
      }
      if (!patch.dryRun) {
        documents[index].object = structuredClone(planned.object ?? { statements: [] });
      }
      return planned;
    },
  };
}

function executeQuery(object: ObjectText, query: Query): ObjectResult {
  const operation = query.operation;
  if (operation.kind === "summary" || operation.kind === "tree" || operation.kind === "context" || operation.kind === "exec_flow" || operation.kind === "data_flow") {
    return { object: structuredClone(object), diagnostics: [] };
  }

  let bindings = object.statements.filter(isBinding);
  let exact = false;
  if (operation.kind === "palette_entries") {
    bindings = bindings.filter(isPaletteBinding).filter((binding) => matchesText(binding, operation.text));
  } else if ("text" in operation || isCollectionKind(operation.kind)) {
    const callee = singular(operation.kind);
    const text = "text" in operation ? operation.text : undefined;
    bindings = bindings.filter((binding) => call(binding)?.callee === callee).filter((binding) => matchesText(binding, text));
  } else if ("name" in operation) {
    exact = true;
    bindings = bindings.filter((binding) => call(binding)?.callee === operation.kind && bindingName(binding) === operation.name);
  } else if ("id" in operation) {
    exact = true;
    bindings = bindings.filter((binding) => operation.kind === "palette"
      ? call(binding)?.args.palette === operation.id
      : call(binding)?.callee === operation.kind && call(binding)?.args.id === operation.id);
  } else {
    return unsupported(operation.kind);
  }

  bindings = bindings.filter((binding) => matchesCondition(binding, query.where));
  bindings = sortBindings(bindings, query);
  const page = paginate(bindings, query.page?.limit ?? 50, query.page?.after);
  const statements: Statement[] = [];
  const aliases = new Set<string>();
  const selectedAliases = new Set(
    page.items
      .filter((binding) => binding.target.kind === "local")
      .map((binding) => (binding.target as LocalRef).name),
  );
  const referencedOwners = new Set(
    page.items
      .filter((binding) => binding.target.kind === "member")
      .map((binding) => binding.target.kind === "member" ? binding.target.object.name : ""),
  );
  for (const owner of referencedOwners) {
    if (selectedAliases.has(owner)) continue;
    const ownerBinding = object.statements.find((statement) =>
      isBinding(statement) && statement.target.kind === "local" && statement.target.name === owner,
    );
    if (ownerBinding) {
      statements.push(structuredClone(ownerBinding));
      aliases.add(owner);
    }
  }
  for (const binding of page.items) {
    statements.push(structuredClone(binding));
    if (binding.target.kind === "local") aliases.add(binding.target.name);
  }
  if (exact) {
    statements.push(...object.statements.filter((statement) => isOwnedMemberBinding(statement, selectedAliases)).map((item) => structuredClone(item)));
  }
  if (query.with?.includes("schema") && bindings.length === 1) {
    statements.push({ kind: "comment", text: "schema\n\nfields:\n  runtime fixture; query the live executor for UE-owned fields" });
  }
  if (statements.length === 0) {
    statements.push({ kind: "comment", text: "no matches" });
  }
  return {
    object: { statements },
    diagnostics: [],
    ...(page.next ? { page: { next: page.next } } : {}),
  };
}

function applyPatch(source: ObjectText, patch: Patch): MutationResult {
  const object = structuredClone(source);
  const declared = new Map<string, Binding>();
  const touched: Statement[] = [];

  for (const statement of patch.statements) {
    if (isBinding(statement)) {
      declared.set(bindingTargetKey(statement.target), structuredClone(statement));
      continue;
    }
    const diagnostic = applyOperation(object, statement, declared, touched);
    if (diagnostic) {
      return mutationResult(patch, undefined, [diagnostic], false, false);
    }
  }
  return mutationResult(patch, { statements: touched.length > 0 ? touched : structuredClone(object.statements) }, [], true, !patch.dryRun);
}

function applyOperation(
  object: ObjectText,
  operation: PatchOperation,
  declared: Map<string, Binding>,
  touched: Statement[],
): ObjectResult["diagnostics"][number] | undefined {
  switch (operation.kind) {
    case "add": {
      const binding = declared.get(bindingTargetKey(operation.target));
      if (!binding) return error("resolution.binding_not_found", `Binding ${bindingTargetKey(operation.target)} was not declared.`);
      object.statements.push(structuredClone(binding));
      touched.push(structuredClone(binding));
      return undefined;
    }
    case "remove": {
      const index = findBindingIndex(object, operation.target);
      if (index < 0) return error("resolution.object_not_found", `Object ${refKey(operation.target)} was not found.`);
      const [removed] = object.statements.splice(index, 1);
      touched.push(structuredClone(removed));
      object.statements = object.statements.filter((statement) => !isEdge(statement) || (!sameRef(statement.from, operation.target) && !sameRef(statement.to, operation.target)));
      return undefined;
    }
    case "set": return setField(object, operation.target, operation.value, touched);
    case "reset": return resetField(object, operation.target, touched);
    case "move": return moveObject(object, operation, touched);
    case "connect": {
      const edge = { from: structuredClone(operation.from), to: structuredClone(operation.to) };
      if (!object.statements.some((statement) => isEdge(statement) && sameEdge(statement, edge))) object.statements.push(edge);
      touched.push(edge);
      return undefined;
    }
    case "disconnect": {
      const index = object.statements.findIndex((statement) => isEdge(statement) && sameEdge(statement, operation));
      if (index < 0) return error("resolution.edge_not_found", "The exact Edge was not found.");
      const [removed] = object.statements.splice(index, 1);
      touched.push(structuredClone(removed));
      return undefined;
    }
    case "break": {
      const removed = object.statements.filter((statement) => isEdge(statement) && (sameRef(statement.from, operation.target) || sameRef(statement.to, operation.target)));
      object.statements = object.statements.filter((statement) => !removed.includes(statement));
      touched.push(...structuredClone(removed));
      return undefined;
    }
    case "insert": {
      const node = localOwner(operation.input);
      const binding = node ? declared.get(node) : undefined;
      if (!binding) return error("resolution.binding_not_found", "Insert input must belong to a declared local binding.");
      const edgeIndex = object.statements.findIndex((statement) => isEdge(statement) && sameRef(statement.from, operation.from) && sameRef(statement.to, operation.to));
      if (edgeIndex < 0) return error("resolution.edge_not_found", "Insert requires the exact existing Edge.");
      object.statements.splice(edgeIndex, 1);
      object.statements.push(structuredClone(binding));
      const first = { from: structuredClone(operation.from), to: structuredClone(operation.input) };
      const second = { from: structuredClone(operation.output), to: structuredClone(operation.to) };
      object.statements.push(first, second);
      touched.push(structuredClone(binding), first, second);
      return undefined;
    }
    case "wrap": {
      const binding = declared.get(operation.with.name);
      if (!binding) return error("resolution.binding_not_found", `Binding ${operation.with.name} was not declared.`);
      object.statements.push(structuredClone(binding));
      touched.push(structuredClone(binding));
      return undefined;
    }
    case "replace": {
      const declaredBinding = isLocalRef(operation.with) ? declared.get(operation.with.name) : undefined;
      let replacement = declaredBinding;
      let replacementIndex = -1;
      if (!replacement) {
        replacementIndex = findBindingIndex(object, operation.with);
        replacement = replacementIndex >= 0 && isBinding(object.statements[replacementIndex])
          ? object.statements[replacementIndex] as Binding
          : undefined;
      }
      if (!replacement) {
        return error(
          isLocalRef(operation.with) ? "resolution.binding_not_found" : "resolution.object_not_found",
          `${isLocalRef(operation.with) ? "Binding" : "Object"} ${refKey(operation.with)} was not found.`,
        );
      }
      let targetIndex = findBindingIndex(object, operation.target);
      if (targetIndex < 0) return error("resolution.object_not_found", `Object ${refKey(operation.target)} was not found.`);
      if (replacementIndex === targetIndex) return error("validation.replace_same_object", "Replacement and target must be different objects.");
      const replacementCopy = structuredClone(replacement);
      if (replacementIndex >= 0) {
        object.statements.splice(replacementIndex, 1);
        if (replacementIndex < targetIndex) targetIndex -= 1;
      }
      object.statements.splice(targetIndex, 1, replacementCopy);
      touched.push(replacementCopy);
      return undefined;
    }
    case "invoke":
      return undefined;
    case "compile":
    case "save":
      return undefined;
  }
}

function setField(object: ObjectText, target: MemberRef, value: Expr, touched: Statement[]) {
  const index = findBindingIndex(object, target.object);
  if (index < 0) {
    if (isLocalRef(target.object)) {
      const binding: Binding = {
        target: { kind: "member", object: structuredClone(target.object), path: [...target.path] as [string, ...string[]] },
        value: structuredClone(value),
      };
      object.statements.push(binding);
      touched.push(structuredClone(binding));
      return undefined;
    }
    return error("resolution.object_not_found", `Object ${refKey(target.object)} was not found.`);
  }
  const binding = object.statements[index];
  if (!isBinding(binding) || !isCallValue(binding.value)) return error("validation.field_unavailable", "The target has no writable Call fields.");
  writePath(binding.value.args, target.path, structuredClone(value));
  touched.push(structuredClone(binding));
  return undefined;
}

function resetField(object: ObjectText, target: MemberRef, touched: Statement[]) {
  const index = findBindingIndex(object, target.object);
  if (index < 0) return error("resolution.object_not_found", `Object ${refKey(target.object)} was not found.`);
  const binding = object.statements[index];
  if (!isBinding(binding) || !isCallValue(binding.value)) return error("validation.field_unavailable", "The target has no resettable Call fields.");
  deletePath(binding.value.args, target.path);
  touched.push(structuredClone(binding));
  return undefined;
}

function moveObject(object: ObjectText, operation: Extract<PatchOperation, { kind: "move" }>, touched: Statement[]) {
  const index = findBindingIndex(object, operation.target);
  if (index < 0) return error("resolution.object_not_found", `Object ${refKey(operation.target)} was not found.`);
  const statement = object.statements[index];
  if (operation.to && Array.isArray(operation.to) && isBinding(statement) && isCallValue(statement.value)) {
    statement.value.args.at = structuredClone(operation.to) as Expr;
  } else if (operation.by && isBinding(statement) && isCallValue(statement.value)) {
    const at = Array.isArray(statement.value.args.at) ? statement.value.args.at : [0, 0];
    statement.value.args.at = [Number(at[0]) + Number(operation.by[0]), Number(at[1]) + Number(operation.by[1])];
  } else {
    const anchor = operation.before ?? operation.after;
    if (anchor) {
      const anchorIndex = findBindingIndex(object, anchor);
      if (anchorIndex < 0) return error("resolution.object_not_found", `Anchor ${refKey(anchor)} was not found.`);
      const [moving] = object.statements.splice(index, 1);
      const adjusted = object.statements.indexOf(object.statements[anchorIndex > index ? anchorIndex - 1 : anchorIndex]);
      object.statements.splice(adjusted + (operation.after ? 1 : 0), 0, moving);
    }
  }
  touched.push(structuredClone(statement));
  return undefined;
}

function mutationResult(
  patch: Patch,
  object: ObjectText | undefined,
  diagnostics: ObjectResult["diagnostics"],
  valid: boolean,
  applied: boolean,
): MutationResult {
  return {
    ...(object ? { object } : {}),
    diagnostics,
    isError: diagnostics.some((item) => item.severity === "error"),
    dryRun: patch.dryRun,
    valid,
    applied,
    operation: "patch",
  };
}

function findDocument(documents: MemoryDocument[], target: Target): MemoryDocument | undefined {
  return documents.find((document) => sameTarget(document.target, target));
}

function sameTarget(left: Target, right: Target): boolean {
  return JSON.stringify(left.value) === JSON.stringify(right.value);
}

function notFound(target: Target): Result {
  return { diagnostics: [error("resolution.target_not_found", `No in-memory object is registered for target ${target.alias}.`)] };
}

function unsupported(kind: string): ObjectResult {
  return { diagnostics: [error("capability.unsupported_query_operation", `The in-memory executor does not implement ${kind}.`)] };
}

function error(code: string, message: string): ObjectResult["diagnostics"][number] {
  return { severity: "error", code, message };
}

function isCollectionKind(kind: string): boolean {
  return ["assets", "variables", "dispatchers", "graphs", "components", "nodes", "properties", "functions", "defaults", "widgets"].includes(kind);
}

function singular(kind: string): string {
  return ({ properties: "property" } as Record<string, string>)[kind] ?? (kind.endsWith("ies") ? `${kind.slice(0, -3)}y` : kind.slice(0, -1));
}

function isBinding(statement: Statement | Patch["statements"][number]): statement is Binding {
  return !("kind" in statement) && "target" in statement && "value" in statement;
}

function isEdge(statement: Statement): statement is { from: Ref; to: Ref } {
  return "from" in statement;
}

function call(binding: Binding): Call | undefined {
  return isCallValue(binding.value) ? binding.value : undefined;
}

function isCallValue(value: Expr): value is Call {
  return typeof value === "object" && value !== null && !Array.isArray(value) && "kind" in value && value.kind === "call" && "args" in value;
}

function isPaletteBinding(binding: Binding): boolean {
  return typeof call(binding)?.args.palette === "string";
}

function bindingName(binding: Binding): string {
  const nativeName = call(binding)?.args.name;
  if (typeof nativeName === "string") return nativeName;
  return binding.target.kind === "local" ? binding.target.name : binding.target.path[binding.target.path.length - 1];
}

function matchesText(binding: Binding, text: string | undefined): boolean {
  if (!text) return true;
  const needle = text.toLowerCase();
  return [bindingName(binding), call(binding)?.callee, ...Object.values(call(binding)?.args ?? {})]
    .some((value) => typeof value === "string" && value.toLowerCase().includes(needle));
}

function matchesCondition(binding: Binding, condition: Condition | undefined): boolean {
  if (!condition) return true;
  switch (condition.kind) {
    case "eq": return equal(readBindingField(binding, condition.field.path), condition.value);
    case "ne": return !equal(readBindingField(binding, condition.field.path), condition.value);
    case "contains": return String(readBindingField(binding, condition.field.path) ?? "").toLowerCase().includes(String(exprValue(condition.value)).toLowerCase());
    case "compare": return compare(readBindingField(binding, condition.field.path), condition.op, exprValue(condition.value));
    case "not": return !matchesCondition(binding, condition.condition);
    case "and": return condition.conditions.every((item) => matchesCondition(binding, item));
    case "or": return condition.conditions.some((item) => matchesCondition(binding, item));
  }
}

function readBindingField(binding: Binding, path: string[]): unknown {
  if (path[0] === "name") return bindingName(binding);
  let value: unknown = call(binding)?.args;
  for (const key of path) {
    if (typeof value !== "object" || value === null || Array.isArray(value)) return undefined;
    value = (value as Record<string, unknown>)[key];
  }
  return exprValue(value);
}

function exprValue(value: unknown): unknown {
  if (typeof value === "object" && value !== null && !Array.isArray(value) && "kind" in value && value.kind === "name" && "name" in value) {
    return value.name;
  }
  return value;
}

function equal(left: unknown, right: Expr): boolean {
  return JSON.stringify(exprValue(left)) === JSON.stringify(exprValue(right));
}

function compare(left: unknown, op: "gt" | "gte" | "lt" | "lte", right: unknown): boolean {
  if ((typeof left !== "number" && typeof left !== "string") || (typeof right !== "number" && typeof right !== "string")) return false;
  if (op === "gt") return left > right;
  if (op === "gte") return left >= right;
  if (op === "lt") return left < right;
  return left <= right;
}

function sortBindings(bindings: Binding[], query: Query): Binding[] {
  if (!query.orderBy) return bindings;
  return [...bindings].sort((left, right) => {
    for (const order of query.orderBy ?? []) {
      const a = readBindingField(left, order.key.split("."));
      const b = readBindingField(right, order.key.split("."));
      const result = String(a ?? "").localeCompare(String(b ?? ""), undefined, { numeric: true });
      if (result !== 0) return order.direction === "desc" ? -result : result;
    }
    return 0;
  });
}

function paginate<T>(items: T[], limit: number, cursor: string | undefined): { items: T[]; next?: string } {
  const match = cursor ? /^offset:(\d+)$/.exec(cursor) : undefined;
  const start = match ? Number(match[1]) : 0;
  const end = start + limit;
  return { items: items.slice(start, end), ...(end < items.length ? { next: `offset:${end}` } : {}) };
}

function isOwnedMemberBinding(statement: Statement, aliases: Set<string>): boolean {
  return isBinding(statement) && statement.target.kind === "member" && aliases.has(statement.target.object.name);
}

function findBindingIndex(object: ObjectText, ref: Ref): number {
  return object.statements.findIndex((statement) => isBinding(statement) && bindingMatchesRef(statement, ref));
}

function bindingMatchesRef(binding: Binding, ref: Ref): boolean {
  if (isLocalRef(ref)) return binding.target.kind === "local" && binding.target.name === ref.name;
  if (isMemberRef(ref)) return binding.target.kind === "member" && sameRef(binding.target, ref);
  return call(binding)?.callee === ref.kind && call(binding)?.args.id === ref.id;
}

function bindingTargetKey(target: BindingTarget): string {
  return target.kind === "local" ? target.name : `${target.object.name}.${target.path.join(".")}`;
}

function refKey(ref: Ref): string {
  if (isLocalRef(ref)) return ref.name;
  if (isMemberRef(ref)) return `${refKey(ref.object)}.${ref.path.join(".")}`;
  return `${ref.kind}@${ref.id}`;
}

function sameRef(left: Ref, right: Ref): boolean {
  return refKey(left) === refKey(right);
}

function sameEdge(left: { from: Ref; to: Ref }, right: { from: Ref; to: Ref }): boolean {
  return sameRef(left.from, right.from) && sameRef(left.to, right.to);
}

function localOwner(ref: Ref): string | undefined {
  return isMemberRef(ref) && isLocalRef(ref.object) ? ref.object.name : undefined;
}

function isLocalRef(ref: LocalRef | StableRef | MemberRef): ref is LocalRef {
  return "name" in ref;
}

function isMemberRef(ref: Ref): ref is MemberRef {
  return "object" in ref;
}

function writePath(object: Record<string, Expr>, path: string[], value: Expr): void {
  let current: Record<string, Expr> = object;
  for (const key of path.slice(0, -1)) {
    const next = current[key];
    if (typeof next !== "object" || next === null || Array.isArray(next) || "kind" in next) current[key] = {};
    current = current[key] as Record<string, Expr>;
  }
  current[path[path.length - 1]] = value;
}

function deletePath(object: Record<string, Expr>, path: string[]): void {
  let current: Record<string, Expr> = object;
  for (const key of path.slice(0, -1)) {
    const next = current[key];
    if (typeof next !== "object" || next === null || Array.isArray(next) || "kind" in next) return;
    current = next as Record<string, Expr>;
  }
  delete current[path[path.length - 1]];
}
