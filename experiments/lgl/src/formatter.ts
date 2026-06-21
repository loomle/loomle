import type { LglObject } from "./index.js";
import { formatAssetLglObject } from "./asset/formatter.js";
import { formatBlueprintLglObject } from "./blueprint/formatter.js";
import { formatGraphLglObject } from "./graph/formatter.js";
import { formatWidgetLglObject } from "./widget/formatter.js";
import { isGraphTarget } from "./core/target.js";

export function formatLglObject(object: LglObject): string {
  if (object.kind === "asset_result") {
    return formatAssetLglObject(object);
  }
  if (object.kind === "blueprint_result") {
    return formatBlueprintLglObject(object);
  }
  if (object.kind === "widget_result") {
    return formatWidgetLglObject(object);
  }
  if (object.kind === "query" && !isGraphTarget(object.target)) {
    if (object.target.domain === "blueprint") {
      return formatBlueprintLglObject(object);
    }
    if (object.target.domain === "widget") {
      return formatWidgetLglObject(object);
    }
    return formatAssetLglObject(object);
  }
  if (object.kind === "patch" && object.target.domain === "blueprint" && !isGraphTarget(object.target)) {
    return formatBlueprintLglObject(object);
  }
  if (object.kind === "patch" && object.target.domain === "widget") {
    return formatWidgetLglObject(object);
  }
  return formatGraphLglObject(object);
}
