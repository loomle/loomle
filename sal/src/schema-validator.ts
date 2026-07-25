import { Ajv2020 } from "ajv/dist/2020.js";
import { salObjectSchemaText } from "./generated/sal-object-schema-data.js";
import type {
  Binding,
  BindingTarget,
  CanonicalTarget,
  Condition,
  Diagnostic,
  ObjectResult,
  ObjectText,
  Patch,
  PatchOperation,
  Query,
  RequestBinding,
  RequestExpr,
  RequestRef,
  ResultExpr,
  ResultRef,
  SalObject,
  Target,
} from "./index.js";
import {
  isLocalRef,
  isMemberRef,
  isObjectExpr,
  isScopedStableRef,
  isStableRef,
} from "./core/expr.js";

type SchemaValidator = (value: unknown) => boolean;

interface SchemaValidators {
  object: SchemaValidator;
  result: SchemaValidator;
}

interface ReferenceContext {
  aliases: Set<string>;
  targetAliases: ReadonlySet<string>;
  stableScopeAliases: ReadonlySet<string>;
  allowUnqualifiedStable: boolean;
  usedTargetAliases: Set<string>;
  bindingTargets: Set<string>;
}

let validators: SchemaValidators | undefined;

export async function validateSalObject(object: SalObject): Promise<Diagnostic | undefined> {
  const validate = loadValidators().object;
  return hasOnlyFiniteNumbers(object) && validate(object) && isReferenceSafeSalObject(object)
    ? undefined
    : diagnostic("language.invalid_object_shape", "Normalized SAL object failed schema validation.");
}

export async function validateObjectResult(result: unknown): Promise<Diagnostic | undefined> {
  const validate = loadValidators().result;
  return hasOnlyFiniteNumbers(result) && validate(result) && isObjectResultContextSafe(result as ObjectResult)
    ? undefined
    : diagnostic("language.invalid_result_shape", "Executor result failed schema or result-context validation.");
}

function hasOnlyFiniteNumbers(value: unknown, seen = new WeakSet<object>()): boolean {
  if (typeof value === "number") return Number.isFinite(value);
  if (value === null || typeof value !== "object") return true;
  if (seen.has(value)) return false;
  seen.add(value);
  const valid = Array.isArray(value)
    ? value.every((item) => hasOnlyFiniteNumbers(item, seen))
    : Object.values(value).every((item) => hasOnlyFiniteNumbers(item, seen));
  seen.delete(value);
  return valid;
}

function loadValidators(): SchemaValidators {
  if (validators) return validators;

  const schema = JSON.parse(salObjectSchemaText) as { $id: string };
  const ajv = new Ajv2020({ allErrors: true, strict: false });
  ajv.addSchema(schema);
  validators = {
    object: ajv.compile({ $ref: `${schema.$id}#/$defs/SalObject` }) as SchemaValidator,
    result: ajv.compile({ $ref: `${schema.$id}#/$defs/ObjectResult` }) as SchemaValidator,
  };
  return validators;
}

function diagnostic(code: string, message: string): Diagnostic {
  return { severity: "error", code, message };
}

function isReferenceSafeSalObject(object: SalObject): boolean {
  if (!("kind" in object)) {
    return validateObjectTextReferences(object, new Set(), new Set(), true).safe;
  }
  return object.kind === "query"
    ? isReferenceSafeQuery(object)
    : isReferenceSafePatch(object);
}

function isReferenceSafeQuery(query: Query): boolean {
  const aliases = new Set([query.target.alias]);
  if (query.where && !isConditionReferenceSafe(query.where, aliases)) return false;
  if ((query.operation.kind === "palette_entries" || query.operation.kind === "palette")
    && "to" in query.operation
    && !isRequestRefSafe(query.operation.to, aliases)) {
    return false;
  }
  return true;
}

function isReferenceSafePatch(patch: Patch): boolean {
  const aliases = new Set([patch.target.alias]);
  const bindingTargets = new Set<string>();
  for (const statement of patch.statements) {
    if (isRequestBinding(statement)) {
      if (!isRequestBindingSafe(statement, aliases, bindingTargets)) return false;
      continue;
    }
    if (!isPatchOperationSafe(statement, aliases)) return false;
    if (statement.kind === "invoke") {
      for (const output of statement.outputs) {
        if (aliases.has(output.alias)) return false;
        aliases.add(output.alias);
      }
    }
  }
  return true;
}

export function isObjectResultContextSafe(result: ObjectResult): boolean {
  if (result.targetContext === "unresolved_target") {
    return !result.object || validateObjectTextReferences(result.object, new Set(), new Set(), false).safe;
  }

  const related = result.relatedTargets ?? [];
  const targetAliases = new Set<string>();
  const targetKeys = new Set<string>();
  targetAliases.add(result.target.alias);
  targetKeys.add(targetKey(result.target.target));

  for (const binding of related) {
    if (targetAliases.has(binding.alias)) return false;
    const key = targetKey(binding.target);
    if (targetKeys.has(key)) return false;
    targetAliases.add(binding.alias);
    targetKeys.add(key);
  }

  const relatedAliases = new Set(related.map((binding) => binding.alias));
  const usedTargetAliases = new Set<string>();
  for (const handoff of result.handoffs ?? []) {
    if (!relatedAliases.has(handoff.target.name)) return false;
    usedTargetAliases.add(handoff.target.name);
  }

  if (result.object) {
    const stableScopeAliases = result.targetContext === "exact_target"
      ? targetAliases
      : relatedAliases;
    const objectValidation = validateObjectTextReferences(
      result.object,
      targetAliases,
      stableScopeAliases,
      result.targetContext === "exact_target",
    );
    if (!objectValidation.safe) return false;
    for (const alias of objectValidation.usedTargetAliases) usedTargetAliases.add(alias);
  }

  return [...relatedAliases].every((alias) => usedTargetAliases.has(alias));
}

function validateObjectTextReferences(
  object: ObjectText,
  targetAliases: ReadonlySet<string>,
  stableScopeAliases: ReadonlySet<string>,
  allowUnqualifiedStable: boolean,
): { safe: boolean; usedTargetAliases: Set<string> } {
  const context: ReferenceContext = {
    aliases: new Set(targetAliases),
    targetAliases,
    stableScopeAliases,
    allowUnqualifiedStable,
    usedTargetAliases: new Set(),
    bindingTargets: new Set(),
  };
  for (const statement of object.statements) {
    if (isResultBinding(statement)) {
      if (!isResultBindingSafe(statement, context)) {
        return { safe: false, usedTargetAliases: context.usedTargetAliases };
      }
    } else if ("from" in statement) {
      if (!isResultRefSafe(statement.from, context) || !isResultRefSafe(statement.to, context)) {
        return { safe: false, usedTargetAliases: context.usedTargetAliases };
      }
    }
  }
  return { safe: true, usedTargetAliases: context.usedTargetAliases };
}

function isRequestBindingSafe(
  binding: RequestBinding,
  aliases: Set<string>,
  targets: Set<string>,
): boolean {
  if (binding.target.kind === "member" && !aliases.has(binding.target.object.name)) return false;
  if (!isRequestExprSafe(binding.value, aliases)) return false;
  const key = bindingTargetKey(binding.target);
  if (targets.has(key)) return false;
  if (binding.target.kind === "local") {
    if (aliases.has(binding.target.name)) return false;
    aliases.add(binding.target.name);
  }
  targets.add(key);
  return true;
}

function isResultBindingSafe(binding: Binding, context: ReferenceContext): boolean {
  if (binding.target.kind === "member") {
    if (!context.aliases.has(binding.target.object.name)) return false;
    noteTargetAlias(binding.target.object.name, context);
  }
  if (!isResultExprSafe(binding.value, context)) return false;
  const key = bindingTargetKey(binding.target);
  if (context.bindingTargets.has(key)) return false;
  if (binding.target.kind === "local") {
    if (context.aliases.has(binding.target.name)) return false;
    context.aliases.add(binding.target.name);
  }
  context.bindingTargets.add(key);
  return true;
}

function isPatchOperationSafe(operation: PatchOperation, aliases: ReadonlySet<string>): boolean {
  switch (operation.kind) {
    case "add":
      return isBindingTargetRefSafe(operation.target, aliases)
        && (!operation.to || isRequestRefSafe(operation.to, aliases))
        && (!operation.before || isRequestRefSafe(operation.before, aliases))
        && (!operation.after || isRequestRefSafe(operation.after, aliases));
    case "remove":
    case "break":
      return isRequestRefSafe(operation.target, aliases);
    case "set":
      return isRequestRefSafe(operation.target, aliases) && isRequestExprSafe(operation.value, aliases);
    case "reset":
      return isRequestRefSafe(operation.target, aliases);
    case "move":
      return isRequestRefSafe(operation.target, aliases)
        && (!operation.to || Array.isArray(operation.to) || isRequestRefSafe(operation.to, aliases))
        && (!operation.before || isRequestRefSafe(operation.before, aliases))
        && (!operation.after || isRequestRefSafe(operation.after, aliases));
    case "connect":
    case "disconnect":
    case "bind":
    case "unbind":
      return isRequestRefSafe(operation.from, aliases) && isRequestRefSafe(operation.to, aliases);
    case "insert":
      return isRequestRefSafe(operation.from, aliases)
        && isRequestRefSafe(operation.input, aliases)
        && isRequestRefSafe(operation.output, aliases)
        && isRequestRefSafe(operation.to, aliases);
    case "wrap":
      return operation.targets.every((target) => isRequestRefSafe(target, aliases))
        && isRequestRefSafe(operation.with, aliases);
    case "replace":
      return isRequestRefSafe(operation.target, aliases) && isRequestRefSafe(operation.with, aliases);
    case "invoke":
      return isRequestRefSafe(operation.target, aliases)
        && Object.values(operation.args).every((value) => isRequestExprSafe(value, aliases));
    case "compile":
    case "save":
      return true;
  }
}

function isConditionReferenceSafe(condition: Condition, aliases: ReadonlySet<string>): boolean {
  switch (condition.kind) {
    case "eq":
    case "ne":
    case "contains":
    case "compare":
      return isRequestExprSafe(condition.value, aliases);
    case "not":
      return isConditionReferenceSafe(condition.condition, aliases);
    case "and":
    case "or":
      return condition.conditions.every((item) => isConditionReferenceSafe(item, aliases));
  }
}

function isRequestExprSafe(expr: RequestExpr, aliases: ReadonlySet<string>): boolean {
  if (expr === null || typeof expr !== "object") return true;
  if (Array.isArray(expr)) return expr.every((item) => isRequestExprSafe(item, aliases));
  if (isObjectExpr(expr)) return Object.values(expr.fields).every((item) => isRequestExprSafe(item, aliases));
  if (isStableRef(expr)) return true;
  if (isLocalRef(expr) || isMemberRef(expr)) return isRequestRefSafe(expr as RequestRef, aliases);
  return expr.kind === "name";
}

function isResultExprSafe(expr: ResultExpr, context: ReferenceContext): boolean {
  if (expr === null || typeof expr !== "object") return true;
  if (Array.isArray(expr)) return expr.every((item) => isResultExprSafe(item, context));
  if (isObjectExpr(expr)) return Object.values(expr.fields).every((item) => isResultExprSafe(item, context));
  if (isStableRef(expr) || isScopedStableRef(expr) || isLocalRef(expr) || isMemberRef(expr)) {
    return isResultRefSafe(expr as ResultRef, context);
  }
  return expr.kind === "name";
}

function isRequestRefSafe(ref: RequestRef, aliases: ReadonlySet<string>): boolean {
  if (isLocalRef(ref)) return aliases.has(ref.name);
  if (isStableRef(ref)) return true;
  return isMemberRef(ref)
    && (isStableRef(ref.object) || (isLocalRef(ref.object) && aliases.has(ref.object.name)));
}

function isResultRefSafe(ref: ResultRef, context: ReferenceContext): boolean {
  if (isLocalRef(ref)) {
    noteTargetAlias(ref.name, context);
    return context.aliases.has(ref.name);
  }
  if (isStableRef(ref)) return context.allowUnqualifiedStable;
  if (isScopedStableRef(ref)) {
    noteTargetAlias(ref.target.name, context);
    return context.stableScopeAliases.has(ref.target.name);
  }
  if (!isMemberRef(ref)) return false;
  return isResultRefSafe(ref.object as ResultRef, context);
}

function noteTargetAlias(alias: string, context: ReferenceContext): void {
  if (context.targetAliases.has(alias)) context.usedTargetAliases.add(alias);
}

function isBindingTargetRefSafe(target: BindingTarget, aliases: ReadonlySet<string>): boolean {
  return target.kind === "local"
    ? aliases.has(target.name)
    : aliases.has(target.object.name);
}

function bindingTargetKey(target: BindingTarget): string {
  return target.kind === "local"
    ? target.name
    : `${target.object.name}:${JSON.stringify(target.path)}`;
}

function targetKey(target: Target | CanonicalTarget): string {
  switch (target.domain) {
    case "asset":
      return JSON.stringify([
        "asset",
        "path" in target ? target.path : null,
        "type" in target ? target.type ?? null : null,
      ]);
    case "blueprint":
      return JSON.stringify(["blueprint", target.asset, target.id ?? null]);
    case "class":
      return JSON.stringify(["class", target.path]);
    case "graph":
      return JSON.stringify([
        "graph",
        target.asset,
        target.blueprintId ?? null,
        "id" in target ? target.id : null,
      ]);
    case "state_tree":
      return JSON.stringify(["state_tree", target.asset, target.type ?? null]);
    case "widget":
      return JSON.stringify(["widget", target.asset, target.id ?? null]);
  }
}

function isRequestBinding(value: Patch["statements"][number]): value is RequestBinding {
  return !("kind" in value) && "target" in value && "value" in value;
}

function isResultBinding(value: ObjectText["statements"][number]): value is Binding {
  return !("kind" in value) && "target" in value && "value" in value;
}
