import type { Asset, AssetResult, Expr, Query, Value } from "../index.js";
import { tryParseBinding } from "../core/binding.js";
import { isCall, isName, symbolName } from "../core/expr.js";
import { parseCondition, parseDetails, parseOrderBy, parsePage } from "../core/condition.js";
import { ParseError, type ParsedLine, spanForLine } from "../core/text.js";

export function parseAssetQuery(lines: ParsedLine[], queryIndex: number): Query {
  const query: Query = {
    kind: "query",
    target: { domain: "asset" },
  };

  for (const line of lines.slice(queryIndex + 1)) {
    if (line.text.startsWith("find ")) {
      query.find = parseAssetFind(line);
    } else if (line.text.startsWith("where ")) {
      query.where = parseCondition(line.text.slice("where ".length), line);
    } else if (line.text.startsWith("with ")) {
      query.with = parseDetails(line.text.slice("with ".length), line);
    } else if (line.text.startsWith("order by ")) {
      query.orderBy = parseOrderBy(line.text.slice("order by ".length), line);
    } else if (line.text.startsWith("page ")) {
      query.page = { ...(query.page ?? {}), ...parsePage(line) };
    } else {
      throw new ParseError("unsupported_query_clause", "Unsupported asset query clause.", spanForLine(line));
    }
  }

  return query;
}

function parseAssetFind(line: ParsedLine): Query["find"] {
  const match = /^find assets(?:\s+"([^"]+)")?$/.exec(line.text);
  if (!match) {
    throw new ParseError("unsupported_asset_query", "Expected find assets [\"text\"].", spanForLine(line));
  }
  return { kind: "assets", ...(match[1] ? { text: match[1] } : {}) };
}

export function tryParseAssetResult(lines: ParsedLine[]): AssetResult | undefined {
  const assets: Asset[] = [];

  for (const line of lines) {
    const binding = tryParseBinding(line);
    if (!binding) {
      return undefined;
    }
    if (binding.target.kind !== "local" || !isCall(binding.value) || binding.value.callee !== "asset") {
      return undefined;
    }
    assets.push(assetFromBinding(binding.target.name, binding.value.args, line));
  }

  return assets.length > 0 ? { kind: "asset_result", assets } : undefined;
}

function assetFromBinding(alias: string, args: Record<string, Expr>, line: ParsedLine): Asset {
  const path = args.path;
  if (typeof path !== "string") {
    throw new ParseError("invalid_asset_binding", "asset(...) requires path: string.", spanForLine(line));
  }

  return {
    alias,
    path,
    ...(symbolName(args.type) ? { type: symbolName(args.type) } : {}),
    ...(typeof args.class === "string" ? { class: args.class } : {}),
    ...(Array.isArray(args.domains) ? { domains: parseStringList(args.domains, line) } : {}),
    ...(typeof args.loaded === "boolean" ? { loaded: args.loaded } : {}),
    ...(isValueRecord(args.registryTags) ? { registryTags: args.registryTags } : {}),
    ...(typeof args.score === "number" ? { score: args.score } : {}),
  };
}

function parseStringList(values: Value[], line: ParsedLine): string[] {
  return values.map((value) => {
    if (typeof value === "string") {
      return value;
    }
    if (isName(value)) {
      return value.name;
    }
    throw new ParseError("invalid_asset_binding", "Asset list values must be strings or symbols.", spanForLine(line));
  });
}

function isValueRecord(value: unknown): value is Record<string, Value> {
  return typeof value === "object" && value !== null && !Array.isArray(value) && !isCall(value);
}
