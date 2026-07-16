import type { SalObject } from "./index.js";
import { formatAssetSalObject } from "./asset/formatter.js";
import { formatBlueprintSalObject } from "./blueprint/formatter.js";
import { formatGraphSalObject } from "./graph/formatter.js";
import { formatWidgetSalObject } from "./widget/formatter.js";
import { isGraphTarget } from "./core/target.js";

export function formatSalObject(object: SalObject): string {
  if (object.kind === "asset_result") {
    return formatAssetSalObject(object);
  }
  if (object.kind === "blueprint_result") {
    return formatBlueprintSalObject(object);
  }
  if (object.kind === "widget_result") {
    return formatWidgetSalObject(object);
  }
  if (object.kind === "query" && !isGraphTarget(object.target)) {
    if (object.target.domain === "blueprint") {
      return formatBlueprintSalObject(object);
    }
    if (object.target.domain === "widget") {
      return formatWidgetSalObject(object);
    }
    return formatAssetSalObject(object);
  }
  if (object.kind === "patch" && object.target.domain === "blueprint" && !isGraphTarget(object.target)) {
    return formatBlueprintSalObject(object);
  }
  if (object.kind === "patch" && object.target.domain === "widget") {
    return formatWidgetSalObject(object);
  }
  if (object.kind === "palette_result" && object.target.domain === "widget") {
    return formatWidgetSalObject(object);
  }
  return formatGraphSalObject(object);
}
