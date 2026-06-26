import type {
  Adapter,
  Asset,
  Condition,
  Expr,
  ObjectResult,
  Query,
  Value,
} from "../index.js";
import { validateQueryCapabilities } from "../core/capabilities.js";
import { isRef } from "../core/expr.js";

export interface CreateMemoryAssetAdapterOptions {
  assets: Asset[];
}

export interface MemoryAssetAdapter extends Adapter {
  getAssets(): Asset[];
}

export function createMemoryAssetAdapter(
  options: CreateMemoryAssetAdapterOptions,
): MemoryAssetAdapter {
  const assets = options.assets.map(cloneAsset);

  return {
    domain: "asset",
    getAssets() {
      return assets.map(cloneAsset);
    },
    async query(query) {
      if (query.target.domain !== "asset") {
        return { diagnostics: [diagnostic("invalid_asset_target", "Asset adapter requires the asset target.")] };
      }
      return executeAssetQuery(assets, query);
    },
  };
}

function executeAssetQuery(assets: Asset[], query: Query): ObjectResult {
  const capabilityDiagnostics = validateQueryCapabilities(query, {
    domain: "asset",
    findKinds: ["assets"],
    whereFields: ["name", "alias", "path", "root", "type", "class", "loaded", "score", "registryTag.*"],
    details: ["registryTags"],
    orderKeys: ["name", "alias", "path", "root", "type", "class", "loaded", "score"],
    supportsPageAfter: true,
    supportsCompare: true,
  });
  if (capabilityDiagnostics.length > 0) {
    return { diagnostics: capabilityDiagnostics };
  }

  const find = query.find;
  if (find && find.kind !== "assets") {
    return { diagnostics: [diagnostic("invalid_asset_find", "Asset adapter can only execute find assets queries.")] };
  }

  const includeRegistryTags = query.with?.includes("registryTags") ?? false;
  const filtered = assets
    .filter((asset) => matchesAssetText(asset, find?.text))
    .filter((asset) => matchesAssetCondition(asset, query.where));
  const page = paginateItems(
    sortItems(filtered, query, (asset, key) => readAssetField(asset, key.split("."))),
    query,
  );

  return {
    object: {
      kind: "asset_result",
      assets: page.items.map((asset) => outputAsset(asset, includeRegistryTags)),
    },
    diagnostics: [],
    ...(page.next ? { page: { next: page.next } } : {}),
  };
}

function matchesAssetText(asset: Asset, text: string | undefined): boolean {
  if (!text) {
    return true;
  }
  const lowered = text.toLowerCase();
  return assetSearchFields(asset).some((field) => field.toLowerCase().includes(lowered));
}

function assetSearchFields(asset: Asset): string[] {
  return [
    asset.alias,
    asset.path,
    asset.type ?? "",
    asset.class ?? "",
    ...(asset.domains ?? []),
    ...Object.keys(asset.registryTags ?? {}),
    ...Object.values(asset.registryTags ?? {}).map(String),
  ];
}

function matchesAssetCondition(asset: Asset, condition: Condition | undefined): boolean {
  if (!condition) {
    return true;
  }

  switch (condition.kind) {
    case "eq":
      return String(readAssetField(asset, condition.field.path)) === exprToConditionString(condition.value);
    case "ne":
      return String(readAssetField(asset, condition.field.path)) !== exprToConditionString(condition.value);
    case "contains":
      return String(readAssetField(asset, condition.field.path)).includes(exprToConditionString(condition.value));
    case "compare":
      return compareValues(readAssetField(asset, condition.field.path), condition.op, condition.value);
    case "not":
      return !matchesAssetCondition(asset, condition.condition);
    case "and":
      return condition.conditions.every((item) => matchesAssetCondition(asset, item));
    case "or":
      return condition.conditions.some((item) => matchesAssetCondition(asset, item));
    default:
      return assertNever(condition);
  }
}

function readAssetField(asset: Asset, path: string[]): unknown {
  const field = path.join(".");
  if (field === "name" || field === "alias") {
    return asset.alias;
  }
  if (field === "path") {
    return asset.path;
  }
  if (field === "root") {
    const match = /^\/[^/]+/.exec(asset.path);
    return match?.[0];
  }
  if (field === "type") {
    return asset.type;
  }
  if (field === "class") {
    return asset.class;
  }
  if (field === "loaded") {
    return asset.loaded;
  }
  if (field === "score") {
    return asset.score;
  }
  if (path[0] === "registryTag" && path[1]) {
    return asset.registryTags?.[path[1]];
  }
  return undefined;
}

function sortItems<T>(items: T[], query: Query, readField: (item: T, key: string) => unknown): T[] {
  if (!query.orderBy || query.orderBy.length === 0) {
    return items;
  }
  return [...items].sort((left, right) => {
    for (const order of query.orderBy ?? []) {
      const result = compareSortable(readField(left, order.key), readField(right, order.key));
      if (result !== 0) {
        return order.direction === "desc" ? -result : result;
      }
    }
    return 0;
  });
}

function paginateItems<T>(items: T[], query: Query): { items: T[]; next?: string } {
  const start = query.page?.after ? cursorOffset(query.page.after) : 0;
  const limit = query.page?.limit ?? 50;
  const end = start + limit;
  return {
    items: items.slice(start, end),
    ...(end < items.length ? { next: `offset:${end}` } : {}),
  };
}

function cursorOffset(cursor: string): number {
  const match = /^offset:(\d+)$/.exec(cursor);
  return match ? Number(match[1]) : 0;
}

function compareSortable(left: unknown, right: unknown): number {
  if (typeof left === "number" && typeof right === "number") {
    return left - right;
  }
  return String(left ?? "").localeCompare(String(right ?? ""));
}

function compareValues(left: unknown, op: "gt" | "gte" | "lt" | "lte", right: Expr): boolean {
  const leftNumber = typeof left === "number" ? left : Number(left);
  const rightNumber = Number(exprToConditionString(right));
  if (Number.isNaN(leftNumber) || Number.isNaN(rightNumber)) {
    return false;
  }
  switch (op) {
    case "gt":
      return leftNumber > rightNumber;
    case "gte":
      return leftNumber >= rightNumber;
    case "lt":
      return leftNumber < rightNumber;
    case "lte":
      return leftNumber <= rightNumber;
    default:
      return assertNever(op);
  }
}

function exprToConditionString(value: Expr): string {
  if (isName(value)) {
    return value.name;
  }
  if (isRef(value)) {
    return value.kind === "member" ? `${value.object}.${value.member}` : value.kind === "id" ? value.id : value.name;
  }
  return String(value);
}

function isName(value: Expr): value is { kind: "name"; name: string } {
  return typeof value === "object" && value !== null && !Array.isArray(value) && value.kind === "name";
}

function outputAsset(asset: Asset, includeRegistryTags: boolean): Asset {
  const copy = cloneAsset(asset);
  if (!includeRegistryTags) {
    delete copy.registryTags;
  }
  return copy;
}

function cloneAsset(asset: Asset): Asset {
  return structuredClone(asset);
}

function diagnostic(code: string, message: string): ObjectResult["diagnostics"][number] {
  return { severity: "error", code, message };
}

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}
