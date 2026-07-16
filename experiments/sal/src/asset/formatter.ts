import type { Asset, AssetResult, Expr, Query } from "../index.js";
import { formatCondition } from "../core/condition.js";
import { formatArgList } from "../core/expr.js";

export function formatAssetSalObject(object: AssetResult | Query): string {
  switch (object.kind) {
    case "asset_result":
      return formatAssetResult(object);
    case "query":
      return formatAssetQuery(object);
    default:
      return assertNever(object);
  }
}

function formatAssetResult(result: AssetResult): string {
  return `${result.assets.map(formatAsset).join("\n")}\n`;
}

function formatAsset(asset: Asset): string {
  const args: Record<string, Expr> = {
    path: asset.path,
    ...(asset.type ? { type: { kind: "name", name: asset.type } } : {}),
    ...(asset.class ? { class: asset.class } : {}),
    ...(asset.domains ? { domains: asset.domains.map((name) => ({ kind: "name", name })) } : {}),
    ...(asset.loaded !== undefined ? { loaded: asset.loaded } : {}),
    ...(asset.registryTags ? { registryTags: asset.registryTags } : {}),
    ...(asset.score !== undefined ? { score: asset.score } : {}),
  };
  return `${asset.alias} = asset(${formatArgList(args)})`;
}

function formatAssetQuery(query: Query): string {
  const lines = ["query asset"];
  if (query.find?.kind === "assets") {
    lines.push(`find assets${query.find.text ? ` ${JSON.stringify(query.find.text)}` : ""}`);
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

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}
