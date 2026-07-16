import { readFile } from "node:fs/promises";
import type { Diagnostic, TextResult } from "./index.js";

const interfaceModules = [
  ["asset", "Find UE assets and exact Asset Paths."],
  ["blueprint", "Inspect and edit Blueprint-owned structure and finalize changes."],
  ["class", "Inspect UE Reflection and edit durable Blueprint Class Defaults."],
  ["graph", "Inspect and edit Graph Nodes, Pins, Edges, flow, and Node creation."],
  ["widget", "Inspect and edit WidgetBlueprint trees, Widgets, placement, and events."],
] as const;

const moduleDescriptions = new Map<string, string>(interfaceModules);

export function normalizeInterfaceModules(interfaces: readonly string[]): string[] {
  const requested = new Set(interfaces);
  const unknown = [...requested].filter((name) => !moduleDescriptions.has(name));
  if (unknown.length > 0) {
    throw new Error(`Unknown SAL interface module${unknown.length === 1 ? "" : "s"}: ${unknown.join(", ")}.`);
  }
  return interfaceModules.map(([name]) => name).filter((name) => requested.has(name));
}

export async function loadInterfaceSchema(
  interfaces: readonly string[],
  module?: string,
): Promise<TextResult> {
  if (module === undefined) {
    return { text: formatInterfaceIndex(interfaces), diagnostics: [] };
  }

  if (!moduleDescriptions.has(module) || !interfaces.includes(module)) {
    return {
      diagnostics: [interfaceUnavailable(module, interfaces)],
    };
  }

  const url = new URL(`../../docs/interfaces/${module}.md`, import.meta.url);
  return { text: await readFile(url, "utf8"), diagnostics: [] };
}

function formatInterfaceIndex(interfaces: readonly string[]): string {
  const entries = interfaces.map((name) => `${name}\n  ${moduleDescriptions.get(name)}`);
  return [
    ...entries,
    "Use sal.schema(\"<module>\") for one static interface.",
    "Use exact with schema for current Query operations, fields, Patch statements, and Operations.",
  ].join("\n\n") + "\n";
}

function interfaceUnavailable(module: string, interfaces: readonly string[]): Diagnostic {
  return {
    severity: "error",
    code: "capability.interface_unavailable",
    message: `SAL interface module ${module} is not active in this executor.`,
    actual: module,
    supported: [...interfaces],
    suggestion: "Run sal.schema() to list active interface modules.",
  };
}
