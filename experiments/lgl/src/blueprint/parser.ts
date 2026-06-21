import type {
  Blueprint,
  BlueprintComponent,
  BlueprintMember,
  BlueprintResult,
  Expr,
  GraphRef,
  Query,
  Value,
} from "../index.js";
import { tryParseBinding } from "../core/binding.js";
import { isCall, isLocalRef, isName, symbolName } from "../core/expr.js";
import { parseCondition, parseDetails, parseOrderBy, parsePage } from "../core/condition.js";
import { ParseError, type ParsedLine, spanForLine } from "../core/text.js";

export interface BlueprintBinding {
  alias: string;
  asset: string;
}

export function parseBlueprintBindings(lines: ParsedLine[]): Map<string, BlueprintBinding> {
  const assets = new Map<string, string>();
  const blueprints = new Map<string, BlueprintBinding>();

  for (const line of lines) {
    if (line.text.startsWith("query ") || line.text.startsWith("patch ")) {
      break;
    }
    const binding = tryParseBinding(line);
    if (!binding || binding.target.kind !== "local" || !isCall(binding.value)) {
      continue;
    }
    if (binding.value.callee === "asset" && typeof binding.value.args.path === "string") {
      assets.set(binding.target.name, binding.value.args.path);
      continue;
    }
    if (binding.value.callee === "blueprint") {
      const asset = binding.value.args.asset;
      if (typeof asset === "string") {
        blueprints.set(binding.target.name, { alias: binding.target.name, asset });
        continue;
      }
      if (!isLocalRef(asset)) {
        throw new ParseError("invalid_blueprint_binding", "blueprint(...) requires asset: assetBinding or asset path.", spanForLine(line));
      }
      const assetPath = assets.get(asset.name);
      if (!assetPath) {
        throw new ParseError("unknown_asset_binding", `Unknown asset binding ${asset.name}.`, spanForLine(line));
      }
      blueprints.set(binding.target.name, { alias: binding.target.name, asset: assetPath });
    }
  }

  return blueprints;
}

export function parseBlueprintQuery(
  lines: ParsedLine[],
  queryIndex: number,
  bindings: Map<string, BlueprintBinding>,
): Query {
  const queryLine = lines[queryIndex];
  const match = /^query\s+([A-Za-z_][A-Za-z0-9_]*)$/.exec(queryLine.text);
  if (!match) {
    throw new ParseError("invalid_query", "Expected query <blueprint>.", spanForLine(queryLine));
  }
  const binding = bindings.get(match[1]);
  if (!binding) {
    throw new ParseError("unknown_blueprint_binding", `Unknown blueprint binding ${match[1]}.`, spanForLine(queryLine));
  }

  const query: Query = {
    kind: "query",
    target: { domain: "blueprint", asset: binding.asset },
  };

  for (const line of lines.slice(queryIndex + 1)) {
    if (line.text.startsWith("find ")) {
      query.find = parseBlueprintFind(line);
    } else if (line.text.startsWith("where ")) {
      query.where = parseCondition(line.text.slice("where ".length), line);
    } else if (line.text.startsWith("with ")) {
      query.with = parseDetails(line.text.slice("with ".length));
    } else if (line.text.startsWith("order by ")) {
      query.orderBy = parseOrderBy(line.text.slice("order by ".length));
    } else if (line.text.startsWith("page ")) {
      query.page = { ...(query.page ?? {}), ...parsePage(line) };
    } else {
      throw new ParseError("unsupported_query_clause", "Unsupported blueprint query clause.", spanForLine(line));
    }
  }

  return query;
}

function parseBlueprintFind(line: ParsedLine): Query["find"] {
  let match = /^find members(?:\s+"([^"]+)")?$/.exec(line.text);
  if (match) {
    return { kind: "members", ...(match[1] ? { text: match[1] } : {}) };
  }
  match = /^find components(?:\s+"([^"]+)")?$/.exec(line.text);
  if (match) {
    return { kind: "components", ...(match[1] ? { text: match[1] } : {}) };
  }
  throw new ParseError("unsupported_blueprint_query", "Expected find members [\"text\"] or find components [\"text\"].", spanForLine(line));
}

export function tryParseBlueprintResult(lines: ParsedLine[]): BlueprintResult | undefined {
  const assets = new Map<string, string>();
  const blueprints = new Map<string, Blueprint>();
  const componentParents = new Map<string, string>();

  for (const line of lines) {
    const binding = tryParseBinding(line);
    if (!binding) {
      return undefined;
    }

    if (binding.target.kind === "local" && isCall(binding.value) && binding.value.callee === "asset") {
      if (typeof binding.value.args.path === "string") {
        assets.set(binding.target.name, binding.value.args.path);
      }
      continue;
    }

    if (binding.target.kind === "local" && isCall(binding.value) && binding.value.callee === "blueprint") {
      blueprints.set(binding.target.name, blueprintFromBinding(binding.target.name, binding.value.args, assets, line));
      continue;
    }

    if (binding.target.kind === "member" && isCall(binding.value)) {
      const owner = blueprints.get(binding.target.object);
      if (owner && isMemberCall(binding.value.callee)) {
        owner.members = [...(owner.members ?? []), memberFromBinding(binding.target.member, binding.value, line)];
        continue;
      }
      if (owner && binding.value.callee === "component") {
        const component = componentFromBinding(binding.target.member, binding.value.args, null, line);
        owner.components = [...(owner.components ?? []), component];
        componentParents.set(component.name, owner.alias);
        continue;
      }
      const parentOwner = componentParents.get(binding.target.object);
      const parentBlueprint = parentOwner ? blueprints.get(parentOwner) : undefined;
      if (parentBlueprint && binding.value.callee === "component") {
        const component = componentFromBinding(binding.target.member, binding.value.args, binding.target.object, line);
        parentBlueprint.components = [...(parentBlueprint.components ?? []), component];
        componentParents.set(component.name, parentBlueprint.alias);
        continue;
      }
    }

    return undefined;
  }

  return blueprints.size > 0 ? { kind: "blueprint_result", blueprints: [...blueprints.values()] } : undefined;
}

function blueprintFromBinding(
  alias: string,
  args: Record<string, Expr>,
  assets: Map<string, string>,
  line: ParsedLine,
): Blueprint {
  const asset = args.asset;
  if (!isLocalRef(asset) && typeof asset !== "string") {
    throw new ParseError("invalid_blueprint_binding", "blueprint(...) requires asset.", spanForLine(line));
  }
  return {
    alias,
    asset: isLocalRef(asset) ? assets.get(asset.name) ?? asset.name : asset,
    ...(typeof args.parent === "string" ? { parent: args.parent } : {}),
    ...(typeof args.namespace === "string" ? { namespace: args.namespace } : {}),
    ...(typeof args.category === "string" ? { category: args.category } : {}),
    ...(typeof args.abstract === "boolean" ? { abstract: args.abstract } : {}),
    ...(typeof args.deprecated === "boolean" ? { deprecated: args.deprecated } : {}),
  };
}

function isMemberCall(callee: string): boolean {
  return ["variable", "function", "macro", "dispatcher", "event"].includes(callee);
}

function memberFromBinding(name: string, call: { callee: string; args: Record<string, Expr> }, line: ParsedLine): BlueprintMember {
  return {
    kind: call.callee as BlueprintMember["kind"],
    name,
    ...(symbolName(call.args.type) ? { type: symbolName(call.args.type) } : {}),
    ...(call.args.default !== undefined && isValue(call.args.default) ? { default: call.args.default } : {}),
    ...(typeof call.args.category === "string" ? { category: call.args.category } : {}),
    ...(parameterMap(call.args.inputs) ? { inputs: parameterMap(call.args.inputs) } : {}),
    ...(parameterMap(call.args.outputs) ? { outputs: parameterMap(call.args.outputs) } : {}),
    ...(typeof call.args.pure === "boolean" ? { pure: call.args.pure } : {}),
    ...(typeof call.args.const === "boolean" ? { const: call.args.const } : {}),
    ...(symbolName(call.args.replication) ? { replication: symbolName(call.args.replication) } : {}),
    ...(typeof call.args.reliable === "boolean" ? { reliable: call.args.reliable } : {}),
    ...(isValueRecord(call.args.metadata) ? { metadata: call.args.metadata } : {}),
    ...(typeof call.args.guid === "string" ? { guid: call.args.guid } : {}),
    ...(isGraphRef(call.args.graph) ? { graph: call.args.graph } : {}),
  };
}

function componentFromBinding(
  name: string,
  args: Record<string, Expr>,
  parent: string | null,
  line: ParsedLine,
): BlueprintComponent {
  const classPath = args.class;
  if (typeof classPath !== "string") {
    throw new ParseError("invalid_component_binding", "component(...) requires class: string.", spanForLine(line));
  }
  const { class: _class, parent: _parent, ...properties } = args;
  return {
    name,
    class: classPath,
    parent,
    ...(Object.keys(properties).length > 0 && isValueRecord(properties) ? { properties } : {}),
  };
}

function isParameterMap(value: Expr | undefined): value is Record<string, string | { kind: "name"; name: string }> {
  return (
    typeof value === "object" &&
    value !== null &&
    !Array.isArray(value) &&
    !("kind" in value) &&
    Object.values(value).every((item) => typeof item === "string" || isName(item))
  );
}

function parameterMap(value: Expr | undefined): Record<string, string> | undefined {
  if (!isParameterMap(value)) {
    return undefined;
  }
  return Object.fromEntries(
    Object.entries(value).map(([key, item]) => [key, typeof item === "string" ? item : item.name]),
  );
}

function isValue(value: Expr): value is Value {
  return !isCall(value) && !isLocalRef(value);
}

function isValueRecord(value: unknown): value is Record<string, Value> {
  return typeof value === "object" && value !== null && !Array.isArray(value) && !isCall(value);
}

function isGraphRef(value: Expr | undefined): value is GraphRef {
  return (
    typeof value === "object" &&
    value !== null &&
    !Array.isArray(value) &&
    (("kind" in value && value.kind === "name") || ("kind" in value && value.kind === "id"))
  );
}
