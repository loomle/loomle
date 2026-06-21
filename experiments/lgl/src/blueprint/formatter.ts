import type { Blueprint, BlueprintComponent, BlueprintMember, BlueprintResult, Expr, Query } from "../index.js";
import { formatCondition } from "../core/condition.js";
import { formatArgList } from "../core/expr.js";

export function formatBlueprintLglObject(object: BlueprintResult | Query): string {
  switch (object.kind) {
    case "blueprint_result":
      return formatBlueprintResult(object);
    case "query":
      return formatBlueprintQuery(object);
    default:
      return assertNever(object);
  }
}

function formatBlueprintResult(result: BlueprintResult): string {
  const lines: string[] = [];
  for (const blueprint of result.blueprints) {
    lines.push(formatBlueprint(blueprint));
    for (const member of blueprint.members ?? []) {
      lines.push(formatMember(blueprint.alias, member));
    }
    for (const component of blueprint.components ?? []) {
      lines.push(formatComponent(blueprint.alias, component));
    }
  }
  return `${lines.join("\n")}\n`;
}

function formatBlueprint(blueprint: Blueprint): string {
  const args: Record<string, Expr> = {
    asset: blueprint.asset,
    ...(blueprint.parent ? { parent: blueprint.parent } : {}),
    ...(blueprint.namespace ? { namespace: blueprint.namespace } : {}),
    ...(blueprint.category ? { category: blueprint.category } : {}),
    ...(blueprint.abstract !== undefined ? { abstract: blueprint.abstract } : {}),
    ...(blueprint.deprecated !== undefined ? { deprecated: blueprint.deprecated } : {}),
  };
  return `${blueprint.alias} = blueprint(${formatArgList(args)})`;
}

function formatMember(owner: string, member: BlueprintMember): string {
  const args: Record<string, Expr> = {
    ...(member.type ? { type: { kind: "name", name: member.type } } : {}),
    ...(member.default !== undefined ? { default: member.default } : {}),
    ...(member.category ? { category: member.category } : {}),
    ...(member.inputs ? { inputs: stringMapToValue(member.inputs) } : {}),
    ...(member.outputs ? { outputs: stringMapToValue(member.outputs) } : {}),
    ...(member.pure !== undefined ? { pure: member.pure } : {}),
    ...(member.const !== undefined ? { const: member.const } : {}),
    ...(member.replication ? { replication: { kind: "name", name: member.replication } } : {}),
    ...(member.reliable !== undefined ? { reliable: member.reliable } : {}),
    ...(member.metadata ? { metadata: member.metadata } : {}),
    ...(member.guid ? { guid: member.guid } : {}),
    ...(member.graph ? { graph: member.graph } : {}),
  };
  return `${owner}.${member.name} = ${member.kind}(${formatArgList(args)})`;
}

function formatComponent(owner: string, component: BlueprintComponent): string {
  const args: Record<string, Expr> = {
    class: component.class,
    ...(component.properties ?? {}),
  };
  const target = component.parent ? `${component.parent}.${component.name}` : `${owner}.${component.name}`;
  return `${target} = component(${formatArgList(args)})`;
}

function formatBlueprintQuery(query: Query): string {
  if (query.target.domain !== "blueprint") {
    throw new Error("Blueprint formatter received a non-blueprint query.");
  }
  const lines = [
    `bpAsset = asset(path: ${JSON.stringify(query.target.asset)}, type: blueprint)`,
    "bp = blueprint(asset: bpAsset)",
    "",
    "query bp",
  ];
  if (query.find?.kind === "members") {
    lines.push(`find members${query.find.text ? ` ${JSON.stringify(query.find.text)}` : ""}`);
  } else if (query.find?.kind === "components") {
    lines.push(`find components${query.find.text ? ` ${JSON.stringify(query.find.text)}` : ""}`);
  }
  if (query.where) {
    lines.push(`where ${formatCondition(query.where)}`);
  }
  if (query.with && query.with.length > 0) {
    lines.push(`with ${query.with.join(", ")}`);
  }
  if (query.orderBy && query.orderBy.length > 0) {
    lines.push(`order by ${query.orderBy.map((item) => `${item.key} ${item.direction}`).join(", ")}`);
  }
  if (query.page?.limit !== undefined) {
    lines.push(`page limit ${query.page.limit}`);
  }
  if (query.page?.after !== undefined) {
    lines.push(`page after ${JSON.stringify(query.page.after)}`);
  }
  return `${lines.join("\n")}\n`;
}

function stringMapToValue(values: Record<string, string>): Record<string, { kind: "name"; name: string }> {
  return Object.fromEntries(Object.entries(values).map(([key, value]) => [key, { kind: "name", name: value }]));
}

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}
