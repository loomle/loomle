import { Ajv2020 } from "ajv/dist/2020.js";
import { salObjectSchemaText } from "./generated/sal-object-schema-data.js";
import type {
  Binding,
  BindingTarget,
  Condition,
  Diagnostic,
  Expr,
  ObjectResult,
  ObjectText,
  Patch,
  PatchOperation,
  Query,
  Ref,
  SalObject,
} from "./index.js";

type SchemaValidator = (value: unknown) => boolean;
interface SchemaValidators {
  object: SchemaValidator;
  result: SchemaValidator;
}

let validators: SchemaValidators | undefined;

export async function validateSalObject(object: SalObject): Promise<Diagnostic | undefined> {
  const validate = loadValidators().object;
  return validate(object) && isReferenceSafeSalObject(object) ? undefined : diagnostic(
    "language.invalid_object_shape",
    "Normalized SAL object failed schema validation.",
  );
}

export async function validateObjectResult(result: unknown): Promise<Diagnostic | undefined> {
  const validate = loadValidators().result;
  const objectResult = result as ObjectResult;
  return validate(result) && (!objectResult.object || isReferenceSafeObjectText(objectResult.object)) ? undefined : diagnostic(
    "language.invalid_result_shape",
    "Executor result failed schema validation.",
  );
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
    return isReferenceSafeObjectText(object);
  }
  if (!isExprReferenceSafe(object.target.value, new Set())) {
    return false;
  }
  return object.kind === "query"
    ? isReferenceSafeQuery(object)
    : isReferenceSafePatch(object);
}

function isReferenceSafeQuery(query: Query): boolean {
  return !query.where || isConditionReferenceSafe(query.where, new Set([query.target.alias]));
}

function isReferenceSafePatch(patch: Patch): boolean {
  if (patch.target.value.kind !== "call") {
    return false;
  }
  const aliases = new Set([patch.target.alias]);
  const targets = new Set<string>();
  for (const statement of patch.statements) {
    if (isBinding(statement)) {
      if (!isBindingSafe(statement, aliases, targets)) return false;
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

function isReferenceSafeObjectText(object: ObjectText): boolean {
  const aliases = new Set<string>();
  const targets = new Set<string>();
  for (const statement of object.statements) {
    if (isBinding(statement)) {
      if (!isBindingSafe(statement, aliases, targets)) return false;
    } else if ("from" in statement) {
      if (!isRefSafe(statement.from, aliases) || !isRefSafe(statement.to, aliases)) return false;
    }
  }
  return true;
}

function isBindingSafe(
  binding: Binding,
  aliases: Set<string>,
  targets: Set<string>,
): boolean {
  if (binding.target.kind === "member" && !aliases.has(binding.target.object.name)) {
    return false;
  }
  if (!isExprReferenceSafe(binding.value, aliases)) {
    return false;
  }
  const key = bindingTargetKey(binding.target);
  if (targets.has(key)) {
    return false;
  }
  if (binding.target.kind === "local") {
    if (aliases.has(binding.target.name)) return false;
    aliases.add(binding.target.name);
  }
  targets.add(key);
  return true;
}

function isPatchOperationSafe(operation: PatchOperation, aliases: ReadonlySet<string>): boolean {
  switch (operation.kind) {
    case "add":
      return isBindingTargetRefSafe(operation.target, aliases)
        && (!operation.to || isRefSafe(operation.to, aliases))
        && (!operation.before || isRefSafe(operation.before, aliases))
        && (!operation.after || isRefSafe(operation.after, aliases));
    case "remove":
    case "break":
      return isRefSafe(operation.target, aliases);
    case "set":
      return isRefSafe(operation.target, aliases) && isExprReferenceSafe(operation.value, aliases);
    case "reset":
      return isRefSafe(operation.target, aliases);
    case "move":
      return isRefSafe(operation.target, aliases)
        && (!operation.to || Array.isArray(operation.to) || isRefSafe(operation.to, aliases))
        && (!operation.before || isRefSafe(operation.before, aliases))
        && (!operation.after || isRefSafe(operation.after, aliases));
    case "connect":
    case "disconnect":
      return isRefSafe(operation.from, aliases) && isRefSafe(operation.to, aliases);
    case "insert":
      return isRefSafe(operation.from, aliases)
        && isRefSafe(operation.input, aliases)
        && isRefSafe(operation.output, aliases)
        && isRefSafe(operation.to, aliases);
    case "wrap":
      return operation.targets.every((target) => isRefSafe(target, aliases))
        && isRefSafe(operation.with, aliases);
    case "replace":
      return isRefSafe(operation.target, aliases) && isRefSafe(operation.with, aliases);
    case "invoke":
      return isRefSafe(operation.target, aliases)
        && Object.values(operation.args).every((value) => isExprReferenceSafe(value, aliases));
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
      return isExprReferenceSafe(condition.value, aliases);
    case "not":
      return isConditionReferenceSafe(condition.condition, aliases);
    case "and":
    case "or":
      return condition.conditions.every((item) => isConditionReferenceSafe(item, aliases));
  }
}

function isExprReferenceSafe(expr: Expr, aliases: ReadonlySet<string>): boolean {
  if (expr === null || typeof expr !== "object") return true;
  if (Array.isArray(expr)) return expr.every((item) => isExprReferenceSafe(item, aliases));
  if ("kind" in expr) {
    if (expr.kind === "local" && "name" in expr) return aliases.has(expr.name as string);
    if (expr.kind === "member" && "object" in expr && "path" in expr) return isRefSafe(expr as Ref, aliases);
    if (expr.kind === "call" && "args" in expr) {
      return Object.values(expr.args as Record<string, Expr>).every((item) => isExprReferenceSafe(item, aliases));
    }
    if ((expr.kind === "name" && "name" in expr) || "id" in expr) return true;
  }
  return Object.values(expr).every((item) => isExprReferenceSafe(item, aliases));
}

function isBindingTargetRefSafe(target: BindingTarget, aliases: ReadonlySet<string>): boolean {
  return target.kind === "local"
    ? aliases.has(target.name)
    : aliases.has(target.object.name);
}

function isRefSafe(ref: Ref, aliases: ReadonlySet<string>): boolean {
  if ("name" in ref) return aliases.has(ref.name);
  if ("object" in ref) {
    return !("name" in ref.object) || aliases.has(ref.object.name);
  }
  return true;
}

function bindingTargetKey(target: BindingTarget): string {
  return target.kind === "local" ? target.name : `${target.object.name}.${target.path.join(".")}`;
}

function isBinding(value: ObjectText["statements"][number] | Patch["statements"][number]): value is Binding {
  return !("kind" in value) && "target" in value && "value" in value;
}
