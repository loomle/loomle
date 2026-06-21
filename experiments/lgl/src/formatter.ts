import type { LglObject } from "./index.js";
import { formatGraphLglObject } from "./graph/formatter.js";

export function formatLglObject(object: LglObject): string {
  return formatGraphLglObject(object);
}
