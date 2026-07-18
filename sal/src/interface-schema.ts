import type { Diagnostic, SalInterface, TextResult } from "./index.js";

export function selectActiveInterfaces(
  catalog: readonly SalInterface[],
  activeNames: readonly string[],
): SalInterface[] {
  const byName = new Map<string, SalInterface>();
  for (const entry of catalog) {
    if (byName.has(entry.name)) {
      throw new Error(`Duplicate SAL interface module: ${entry.name}.`);
    }
    byName.set(entry.name, entry);
  }

  const requested = new Set(activeNames);
  const unknown = [...requested].filter((name) => !byName.has(name));
  if (unknown.length > 0) {
    throw new Error(`Unknown SAL interface module${unknown.length === 1 ? "" : "s"}: ${unknown.join(", ")}.`);
  }
  return catalog.filter((entry) => requested.has(entry.name));
}

export async function loadInterfaceSchema(
  interfaces: readonly SalInterface[],
  module?: string,
): Promise<TextResult> {
  if (module === undefined) {
    return { text: formatInterfaceIndex(interfaces), diagnostics: [] };
  }

  const selected = interfaces.find((entry) => entry.name === module);
  if (!selected) {
    return {
      diagnostics: [interfaceUnavailable(module, interfaces)],
    };
  }

  return { text: selected.text, diagnostics: [] };
}

function formatInterfaceIndex(interfaces: readonly SalInterface[]): string {
  const entries = interfaces.map(({ name, description }) => `${name}\n  ${description}`);
  return [
    ...entries,
    "Request one module by name to read its static interface.",
    "Use exact with schema for current Query operations, fields, Patch statements, and Operations.",
  ].join("\n\n") + "\n";
}

function interfaceUnavailable(module: string, interfaces: readonly SalInterface[]): Diagnostic {
  return {
    severity: "error",
    code: "capability.interface_unavailable",
    message: `SAL interface module ${module} is not active in this executor.`,
    actual: module,
    supported: interfaces.map((entry) => entry.name),
    suggestion: "List active interface modules, then request a supported module by name.",
  };
}
