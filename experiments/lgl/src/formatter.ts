import type { LglObject } from "./index.js";
import { formatAssetLglObject } from "./asset/formatter.js";
import { formatGraphLglObject } from "./graph/formatter.js";
import { isGraphTarget } from "./core/target.js";

export function formatLglObject(object: LglObject): string {
  if (object.kind === "asset_result") {
    return formatAssetLglObject(object);
  }
  if (object.kind === "query" && !isGraphTarget(object.target)) {
    return formatAssetLglObject(object);
  }
  return formatGraphLglObject(object);
}
