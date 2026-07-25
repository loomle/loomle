import type {
  Binding,
  CanonicalTarget,
  Diagnostic,
  ExactMutationResult,
  ObjectText,
  Patch,
  PatchResult,
  Query,
  QueryResult,
  RequestBinding,
  ResultExpr,
  SalExecutor,
  Target,
  TargetBinding,
} from "../../src/index.js";

export interface MemoryDocument {
  target: TargetBinding;
  object: ObjectText;
}

export interface CreateMemoryExecutorOptions {
  interfaces: readonly string[];
  documents: MemoryDocument[];
}

export interface MemoryExecutor extends SalExecutor {
  getDocuments(): MemoryDocument[];
}

export function createMemoryExecutor(options: CreateMemoryExecutorOptions): MemoryExecutor {
  let documents = structuredClone(options.documents);

  return {
    interfaces: [...options.interfaces],
    getDocuments() {
      return structuredClone(documents);
    },
    async query(query) {
      const document = documents.find((candidate) => sameTarget(candidate.target.target, query.target.target));
      if (!document) return unresolvedQuery(`Target ${query.target.alias} was not found.`);
      if (isAssetRoot(query.target.target)) {
        return {
          targetContext: "domain_root",
          target: { alias: query.target.alias, target: query.target.target },
          object: structuredClone(document.object),
          diagnostics: [],
        };
      }
      if (!isCanonicalTarget(query.target.target)) {
        return unresolvedQuery(`Target ${query.target.alias} is not canonical in the memory fixture.`);
      }
      return {
        targetContext: "exact_target",
        target: { alias: query.target.alias, target: query.target.target },
        object: structuredClone(document.object),
        diagnostics: [],
      };
    },
    async patch(patch) {
      const index = documents.findIndex((candidate) => sameTarget(candidate.target.target, patch.target.target));
      if (index < 0) return unresolvedMutation(patch, `Target ${patch.target.alias} was not found.`);

      const next = structuredClone(documents[index].object);
      const declared = new Map<string, RequestBinding>();
      const touched: ObjectText["statements"] = [];
      for (const statement of patch.statements) {
        if (!("kind" in statement)) {
          declared.set(bindingKey(statement), statement);
          continue;
        }
        if (statement.kind === "add") {
          const binding = declared.get(bindingTargetKey(statement.target));
          if (!binding) return unresolvedMutation(patch, "Add binding was not declared.");
          const stored = structuredClone(binding) as Binding;
          next.statements.push(stored);
          touched.push(stored);
        } else if (statement.kind === "connect") {
          const edge = structuredClone(statement) as unknown as { from: any; to: any };
          const stored = { from: edge.from, to: edge.to };
          next.statements.push(stored);
          touched.push(stored);
        } else if (statement.kind === "set") {
          const binding = findObjectBinding(next, statement.target.object);
          if (!binding || !isObjectValue(binding.value)) {
            return unresolvedMutation(patch, "Set target was not found.");
          }
          setObjectPath(binding.value.fields, statement.target.path, structuredClone(statement.value) as ResultExpr);
          touched.push(structuredClone(binding));
        }
      }

      if (!patch.dryRun) documents[index].object = next;
      return {
        targetContext: "exact_target",
        target: structuredClone(patch.target),
        object: { statements: touched },
        diagnostics: [],
        isError: false,
        dryRun: patch.dryRun,
        valid: true,
        applied: !patch.dryRun,
        operation: "patch",
      };
    },
  };
}

function unresolvedQuery(message: string): QueryResult {
  return {
    targetContext: "unresolved_target",
    diagnostics: [error("resolution.target_not_found", message)],
  };
}

function unresolvedMutation(patch: Patch, message: string): PatchResult {
  return unresolvedMutationResult(patch, message);
}

function unresolvedMutationResult(patch: Patch, message: string) {
  return {
    targetContext: "unresolved_target" as const,
    diagnostics: [error("resolution.target_not_found", message)] as [Diagnostic],
    isError: true as const,
    dryRun: patch.dryRun,
    valid: false as const,
    applied: false as const,
    operation: "patch",
  };
}

function error(code: string, message: string): Diagnostic {
  return { severity: "error", code, message };
}

function sameTarget(left: Target, right: Target): boolean {
  return targetKey(left) === targetKey(right);
}

function targetKey(target: Target): string {
  return JSON.stringify(
    Object.fromEntries(Object.entries(target).sort(([left], [right]) => left.localeCompare(right))),
  );
}

function isAssetRoot(target: Target): target is Extract<Target, { domain: "asset" }> {
  return target.domain === "asset" && !("path" in target);
}

function isCanonicalTarget(target: Target): target is CanonicalTarget {
  switch (target.domain) {
    case "asset": return "path" in target && typeof target.type === "string";
    case "blueprint": return typeof target.id === "string";
    case "class": return true;
    case "graph": return "id" in target && typeof target.blueprintId === "string" && !("name" in target);
    case "state_tree": return typeof target.type === "string";
    case "widget": return typeof target.id === "string";
  }
}

function bindingKey(binding: RequestBinding): string {
  return bindingTargetKey(binding.target);
}

function bindingTargetKey(target: RequestBinding["target"]): string {
  return target.kind === "local"
    ? target.name
    : `${target.object.name}:${JSON.stringify(target.path)}`;
}

function findObjectBinding(
  object: ObjectText,
  target: Patch["statements"][number] extends never ? never : any,
): Binding | undefined {
  if (target.kind === "local") {
    return object.statements.find(
      (statement): statement is Binding => isBinding(statement)
        && statement.target.kind === "local"
        && statement.target.name === target.name,
    );
  }
  if (target.kind === "stable_ref") {
    const id = target.identityPath[target.identityPath.length - 1];
    return object.statements.find(
      (statement): statement is Binding => isBinding(statement)
        && statement.target.kind === "local"
        && isObjectValue(statement.value)
        && statement.value.fields.id === id,
    );
  }
  return undefined;
}

function setObjectPath(
  fields: Record<string, ResultExpr>,
  path: readonly (string | number)[],
  value: ResultExpr,
): void {
  if (path.length !== 1 || typeof path[0] !== "string") {
    throw new Error("Memory fixture supports one string field in set.");
  }
  fields[path[0]] = value;
}

function isObjectValue(value: ResultExpr): value is Extract<ResultExpr, { kind: "object" }> {
  return typeof value === "object"
    && value !== null
    && !Array.isArray(value)
    && value.kind === "object";
}

function isBinding(statement: ObjectText["statements"][number]): statement is Binding {
  return !("kind" in statement) && "target" in statement && "value" in statement;
}
