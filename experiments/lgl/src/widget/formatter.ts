import type { Expr, Query, WidgetDocument, WidgetNode, WidgetResult } from "../index.js";
import { formatCondition } from "../core/condition.js";
import { formatArgList } from "../core/expr.js";

export function formatWidgetLglObject(object: WidgetResult | Query): string {
  switch (object.kind) {
    case "widget_result":
      return formatWidgetResult(object);
    case "query":
      return formatWidgetQuery(object);
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

function assertNever(value: never): never {
  throw new Error(`Unexpected value: ${String(value)}`);
}
