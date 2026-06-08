import type { BindingContext, BindingSource, BindingWhere, PatchDocument, PatchStatement } from "./ir.js";

export interface CommandPlan {
  graph: string;
  paletteBindings: CompiledBinding[];
  commands: AbstractCommand[];
}

export interface CompiledBinding {
  symbol: string;
  source: BindingSource;
  context?: BindingContext;
  where?: BindingWhere[];
}

export type AbstractCommand =
  | {
      kind: "setProperty";
      node: string;
      property: string;
      value: unknown;
    }
  | {
      kind: "addNode";
      alias: string;
      symbol: string;
      args: unknown[];
    }
  | {
      kind: "rewire";
      from: { node: string; pin: string };
      to: { node: string; pin: string };
    }
  | {
      kind: "connect";
      from: { node: string; pin: string };
      to: { node: string; pin: string };
    };

export function compilePatch(document: PatchDocument): CommandPlan {
  return {
    graph: document.name,
    paletteBindings: document.bindings.map((binding) => ({
      symbol: binding.symbol,
      source: binding.source,
      ...(binding.context ? { context: binding.context } : {}),
      ...(binding.where ? { where: binding.where } : {})
    })),
    commands: document.statements.map(compileStatement)
  };
}

function compileStatement(statement: PatchStatement): AbstractCommand {
  switch (statement.kind) {
    case "set":
      return {
        kind: "setProperty",
        node: statement.target.node,
        property: statement.target.property,
        value: literalValue(statement.value)
      };
    case "add":
      return {
        kind: "addNode",
        alias: statement.node.alias,
        symbol: statement.node.type,
        args: statement.node.args.map(literalValue)
      };
    case "rewire":
      return { kind: "rewire", from: statement.from, to: statement.to };
    case "connect":
      return { kind: "connect", from: statement.from, to: statement.to };
  }
}

function literalValue(literal: { value: unknown }): unknown {
  return literal.value;
}
