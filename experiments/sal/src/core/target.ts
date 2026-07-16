import type { GraphTarget, Target } from "../index.js";

export function isGraphTarget(target: Target): target is GraphTarget {
  return "asset" in target && "graph" in target;
}
