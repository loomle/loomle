import type { Binding, CreationEntry, CreationResult, Expr, Patch, Query, WidgetDocument, WidgetNode, WidgetPatchOp, WidgetProperty, WidgetResult } from "../index.js";
import { formatBindingTarget } from "../core/binding.js";
import { formatCondition } from "../core/condition.js";
import { formatArgList, formatExpr, nameValue } from "../core/expr.js";

export function formatWidgetLglObject(object: WidgetResult | Query | Patch | CreationResult): string {
  switch (object.kind) {
    case "widget_result":
      return formatWidgetResult(object);
    case "query":
      return formatWidgetQuery(object);
    case "patch":
      return formatWidgetPatch(object);
    case "creation_result":
      return formatWidgetCreationResult(object);
    default:
      return assertNever(object);
  }
}

function formatWidgetResult(result: WidgetResult): string {
  const lines: string[] = [];
  for (const document of result.documents) {
    if (lines.length > 0) {
      lines.push("");
    }
    lines.push(formatDocument(document));
    for (const node of document.widgets) {
      lines.push(formatNode(node));
    }
  }
  return `${lines.join("\n")}\n`;
}

function formatDocument(document: WidgetDocument): string {
  return `${document.alias} = widget(asset: ${JSON.stringify(document.asset)}, root: ${document.root})`;
}

function formatNode(node: WidgetNode): string {
  const args: Record<string, Expr> = {
    ...(node.properties ?? {}),
  };
  const target = node.parent ? `${node.parent}.${node.alias}` : node.alias;
  return `${target} = ${node.class}(${formatArgList(args)})`;
}

function formatWidgetQuery(query: Query): string {
  if (query.target.domain !== "widget" || !("asset" in query.target)) {
    throw new Error("Widget formatter received a non-widget query.");
  }
  const lines = [
    `widgetAsset = asset(path: ${JSON.stringify(query.target.asset)}, type: widget)`,
    "w = widget(asset: widgetAsset, root: root)",
    "",
    "query w",
  ];
  if (query.find?.kind === "tree") {
    lines.push("find tree");
  } else if (query.find?.kind === "widgets") {
    lines.push(`find widgets${query.find.text ? ` ${JSON.stringify(query.find.text)}` : ""}`);
  } else if (query.find?.kind === "palette_entry") {
    lines.push(`find palette entry${query.find.text ? ` ${JSON.stringify(query.find.text)}` : ""}`);
  }
  if (query.where) {
    lines.push(`where ${formatCondition(query.where)}`);
  }
  if (query.with && query.with.length > 0) {
    lines.push(`with ${query.with.join(", ")}`);
  }
  if (query.orderBy && query.orderBy.length > 0) {
    lines.push(`order by ${query.orderBy.map((item) => `${item.key} ${item.direction}`).join(", ")}`);
  }
  if (query.page?.limit !== undefined) {
    lines.push(`page limit ${query.page.limit}`);
  }
  if (query.page?.after !== undefined) {
    lines.push(`page after ${JSON.stringify(query.page.after)}`);
  }
  return `${lines.join("\n")}\n`;
}

function formatWidgetCreationResult(result: CreationResult): string {
  if (result.target.domain !== "widget" || !("asset" in result.target)) {
    throw new Error("Widget formatter received a non-widget creation result.");
  }
  const lines = [
    `widgetAsset = asset(path: ${JSON.stringify(result.target.asset)}, type: widget)`,
    "w = widget(asset: widgetAsset, root: root)",
    "",
    ...result.entries.flatMap(formatCreationEntry),
  ];
  return `${lines.join("\n")}\n`;
}

function formatCreationEntry(entry: CreationEntry): string[] {
  const lines: string[] = [];
  if ("class" in entry) {
    lines.push(`${entry.name} = widget(${formatArgList({
      class: entry.class,
      ...(entry.defaults ?? {}),
    })})`);
  } else if ("palette" in entry) {
    lines.push(`${entry.name} = widget(${formatArgList({
      palette: entry.palette.id,
      ...(entry.defaults ?? {}),
    })})`);
  } else {
    lines.push(`${entry.name} = ${formatExpr({
      ...entry.constructor,
      args: {
        ...entry.constructor.args,
        ...(entry.defaults ?? {}),
      },
    })}`);
  }
  lines.push(...(entry.properties ?? []).map((property) => formatWidgetProperty(entry.name, property)));
  return lines;
}

function formatWidgetProperty(entryName: string, property: WidgetProperty): string {
  const args: Record<string, Expr> = {
    type: nameValue(property.type),
    ...(property.default !== undefined ? { default: property.default } : {}),
    ...(property.writable !== undefined ? { writable: property.writable } : {}),
    ...(property.category !== undefined ? { category: property.category } : {}),
  };
  return `${entryName}.${property.name} = property(${formatArgList(args)})`;
}

function formatWidgetPatch(patch: Patch): string {
  if (patch.target.domain !== "widget" || !("asset" in patch.target)) {
    throw new Error("Widget formatter received a non-widget patch.");
  }
  const lines = [
    `widgetAsset = asset(path: ${JSON.stringify(patch.target.asset)}, type: widget)`,
    "w = widget(asset: widgetAsset, root: root)",
    "",
    `patch w${patch.dryRun ? " dry run" : ""}`,
  ];
  if (patch.bindings.length > 0) {
    lines.push("", ...patch.bindings.map(formatBinding));
  }
  if (patch.ops.length > 0) {
    lines.push(...patch.ops.map((op) => formatWidgetOp(asWidgetOp(op))));
  }
  return `${lines.join("\n")}\n`;
}

function formatBinding(binding: Binding): string {
  if (isNodeCreation(binding.value)) {
    throw new Error("Widget formatter received a graph node creation binding.");
  }
  return `${formatBindingTarget(binding.target)} = ${formatExpr(binding.value)}`;
}

function isNodeCreation(value: Binding["value"]): value is Exclude<Binding["value"], Expr> {
  return (
    typeof value === "object" &&
    value !== null &&
    "kind" in value &&
    (value.kind === "palette_node" || value.kind === "shortcut_node")
  );
}

function formatWidgetOp(op: WidgetPatchOp): string {
  switch (op.kind) {
    case "add":
      return `add ${op.target.path.join(".")}`;
    case "set":
      return `set ${op.target.path.join(".")} = ${formatExpr(op.value)}`;
    case "remove":
      return `remove ${op.target.path.join(".")}`;
    case "move":
      return `move ${op.target.path.join(".")} ${op.position} ${op.relativeTo.path.join(".")}`;
    default:
      return assertNever(op);
  }
}

function asWidgetOp(op: Patch["ops"][number]): WidgetPatchOp {
  if (!("target" in op) || !("path" in op.target)) {
    throw new Error("Widget formatter received a non-widget patch operation.");
  }
  return op as WidgetPatchOp;
}

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}
