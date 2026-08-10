import type {
  Binding,
  CanonicalTargetBinding,
  CanonicalTarget,
  DomainRootTargetBinding,
  LocalRef,
  RequestMemberRef,
  ObjectResult,
  ObjectText,
  Page,
  ParseResult,
  Patch,
  PatchTarget,
  PatchOperation,
  PatchStatement,
  Query,
  QueryTarget,
  QueryTargetBinding,
  QueryOperation,
  RequestBinding,
  RequestRef,
  ResultRef,
  StableMemberRef,
  StableRef,
  TargetHandoff,
  TargetSelfMemberRef,
  TargetSelfRef,
} from "./index.js";
import { parseBindingTarget, tryParseBinding, tryParseResultBinding } from "./core/binding.js";
import { parseCondition, parseDetails, parseOrderBy, parsePage } from "./core/condition.js";
import {
  domainKeywords,
  type ExpressionParseOptions,
  isLocalIdentifier,
  isLocalRef,
  isMemberRef,
  isStableRef,
  parseExpr,
  parsePoint,
  parseRef,
  parseResultRef,
  tryParseCall,
  type LegacyCall,
} from "./core/expr.js";
import { findTopLevel, ParseError, type ParsedLine, preprocessLines, spanForLine, splitTopLevelExact, unwrap } from "./core/text.js";
import { isObjectResultContextSafe } from "./schema-validator.js";

const collectionKinds = new Set([
  "assets",
  "actors",
  "variables",
  "dispatchers",
  "graphs",
  "components",
  "nodes",
  "properties",
  "functions",
  "defaults",
  "widgets",
  "states",
  "parameters",
]);

const namedKinds = new Set([
  "variable",
  "dispatcher",
  "graph",
  "component",
  "property",
  "function",
  "default",
  "widget",
]);

const queryHeader = /^query\s+[A-Za-z_][A-Za-z0-9_]*$/;
const patchHeader = /^patch\s+[A-Za-z_][A-Za-z0-9_]*(?:\s+dry run)?$/;

export interface ParseSalOptions {
  compatibility?: "legacy";
  legacyDomain?: QueryTarget["domain"];
}

export type ParsedResultText =
  | {
      targetContext: "exact_target";
      target: CanonicalTargetBinding;
      relatedTargets?: CanonicalTargetBinding[];
      handoffs?: TargetHandoff[];
      object?: ObjectText;
    }
  | {
      targetContext: "domain_root";
      target: DomainRootTargetBinding;
      relatedTargets?: CanonicalTargetBinding[];
      handoffs?: TargetHandoff[];
      object?: ObjectText;
    }
  | {
      targetContext: "unresolved_target";
      object?: ObjectText;
    };

export interface ParseResultTextResult {
  result?: ParsedResultText;
  diagnostics: ParseResult["diagnostics"];
}

export type CanonicalEditorTarget = Extract<
  CanonicalTarget,
  { domain: "blueprint" | "graph" }
>;

export interface ParseCanonicalTargetTextResult {
  target?: CanonicalEditorTarget;
  diagnostics: ParseResult["diagnostics"];
}

interface ParsedPrelude {
  targets: Map<string, QueryTargetBinding>;
  legacyCalls: Map<string, LegacyCall>;
}

export function parseSalObject(text: string, options: ParseSalOptions = {}): ParseResult {
  try {
    const lines = preprocessLines(text);
    if (lines.length === 0) {
      return { object: { statements: [] }, diagnostics: [] };
    }
    const requestIndex = lines.findIndex(
      (line) => line.kind === "code" && (queryHeader.test(line.text) || patchHeader.test(line.text)),
    );
    if (requestIndex < 0) {
      return { object: parseObjectText(lines, new Set(), options), diagnostics: [] };
    }

    const prelude = parseLeadingTargets(lines.slice(0, requestIndex), options);
    const header = lines[requestIndex];
    if (queryHeader.test(header.text)) {
      return { object: parseQuery(lines, requestIndex, prelude, options), diagnostics: [] };
    }
    return { object: parsePatch(lines, requestIndex, prelude, options), diagnostics: [] };
  } catch (error) {
    if (error instanceof ParseError) {
      return {
        diagnostics: [{ severity: "error", code: error.code, message: error.message, span: error.span }],
      };
    }
    throw error;
  }
}

/**
 * Parses the deliberately narrow standalone Target expression accepted by
 * Editor controls. This is not a general SAL document parser: aliases,
 * requests, discovery Targets, and non-Blueprint/Graph Domains are rejected.
 */
export function parseCanonicalTargetText(text: string): ParseCanonicalTargetTextResult {
  try {
    const lines = preprocessLines(text);
    if (lines.length === 0) {
      throw new ParseError(
        "language.invalid_target",
        "Expected exactly one bare canonical Blueprint or Graph Target expression.",
        { line: 1, column: 1 },
      );
    }
    if (lines.length !== 1 || lines[0].kind !== "code") {
      throw new ParseError(
        "language.invalid_target",
        "Expected exactly one bare canonical Blueprint or Graph Target expression without aliases, requests, or comments.",
        spanForLine(lines.length > 1 ? lines[1] : lines[0]),
      );
    }

    const target = parseTargetExpression(lines[0].text, lines[0]);
    if (target.domain !== "blueprint" && target.domain !== "graph") {
      throw new ParseError(
        "language.invalid_target_domain",
        "Editor Target domain must be blueprint or graph.",
        spanForLine(lines[0]),
      );
    }
    if (!isCanonicalEditorTarget(target)) {
      throw new ParseError(
        "language.incomplete_target",
        target.domain === "blueprint"
          ? "Canonical Blueprint Target requires asset and id."
          : "Canonical Graph Target requires asset, blueprintId, and id without name.",
        spanForLine(lines[0]),
      );
    }
    return { target, diagnostics: [] };
  } catch (error) {
    if (error instanceof ParseError) {
      return {
        diagnostics: [{ severity: "error", code: error.code, message: error.message, span: error.span }],
      };
    }
    throw error;
  }
}

export function parseSalResultText(text: string): ParseResultTextResult {
  try {
    const lines = preprocessLines(text);
    const codeLines = lines.filter((line) => line.kind === "code");
    const first = codeLines[0];
    const header = first && /^result\s+(exact_target|domain_root|unresolved_target)$/.exec(first.text);
    if (!first || !header) {
      throw new ParseError(
        "language.invalid_result_header",
        "Expected result exact_target, result domain_root, or result unresolved_target.",
        first ? spanForLine(first) : { line: 1, column: 1 },
      );
    }
    const firstIndex = lines.indexOf(first);
    if (firstIndex !== 0) {
      throw new ParseError(
        "language.invalid_result_envelope",
        "Result Text must begin with its result context; comments belong only inside the objects section.",
        spanForLine(lines[0]),
      );
    }
    const sectionIndex = lines.findIndex(
      (line, index) => index > firstIndex && line.kind === "code"
        && (line.text === "objects" || line.text === "no_objects"),
    );
    if (sectionIndex < 0) {
      throw new ParseError("language.missing_object_section", "Result Text requires objects or no_objects.", spanForLine(first));
    }

    let main: QueryTargetBinding | undefined;
    const related: CanonicalTargetBinding[] = [];
    const handoffs: TargetHandoff[] = [];
    let stage: "target" | "related" | "handoff" = "target";
    for (const line of lines.slice(firstIndex + 1, sectionIndex)) {
      if (line.kind === "comment") {
        throw new ParseError(
          "language.invalid_result_envelope",
          "Comments are only allowed inside the objects section.",
          spanForLine(line),
        );
      }
      const targetLine = /^target\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+)$/.exec(line.text);
      if (targetLine) {
        if (stage !== "target" || main) {
          throw new ParseError("language.invalid_result_envelope", "Result Text permits one leading target line.", spanForLine(line));
        }
        if (!isLocalIdentifier(targetLine[1])) {
          throw new ParseError(
            "language.invalid_target_binding",
            `${targetLine[1]} is reserved and cannot be a Target alias.`,
            spanForLine(line),
          );
        }
        main = { alias: targetLine[1], target: parseTargetExpression(targetLine[2], line) };
        stage = "related";
        continue;
      }
      const relatedLine = /^related\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+)$/.exec(line.text);
      if (relatedLine) {
        if (stage === "handoff") {
          throw new ParseError("language.invalid_result_envelope", "Related Targets must precede handoffs.", spanForLine(line));
        }
        if (!isLocalIdentifier(relatedLine[1])) {
          throw new ParseError(
            "language.invalid_target_binding",
            `${relatedLine[1]} is reserved and cannot be a Target alias.`,
            spanForLine(line),
          );
        }
        const target = parseTargetExpression(relatedLine[2], line);
        if (!isCanonicalTarget(target)) {
          throw new ParseError(
            "language.incomplete_related_target",
            "Related Target requires a canonical exact Target.",
            spanForLine(line),
          );
        }
        related.push({ alias: relatedLine[1], target });
        stage = "related";
        continue;
      }
      const handoffLine = /^handoff\s+("(?:[^"\\]|\\.)*"|[A-Za-z_][A-Za-z0-9_]*)\s+to\s+([A-Za-z_][A-Za-z0-9_]*)$/.exec(line.text);
      if (handoffLine) {
        const purpose = handoffLine[1].startsWith('"')
          ? parseQuotedString(handoffLine[1], line)
          : handoffLine[1];
        if (purpose.length === 0 || !isLocalIdentifier(handoffLine[2])) {
          throw new ParseError(
            "language.invalid_result_envelope",
            "Handoff purpose must be non-empty and its Target must be a legal local alias.",
            spanForLine(line),
          );
        }
        handoffs.push({
          kind: "target_handoff",
          purpose,
          target: { kind: "local", name: handoffLine[2] },
        });
        stage = "handoff";
        continue;
      }
      throw new ParseError("language.invalid_result_envelope", `Invalid Result Text line: ${line.text}`, spanForLine(line));
    }

    const context = header[1] as ParsedResultText["targetContext"];
    if (context === "unresolved_target") {
      if (main || related.length > 0 || handoffs.length > 0) {
        throw new ParseError(
          "language.unresolved_target_has_table",
          "unresolved_target forbids Target and handoff lines.",
          spanForLine(first),
        );
      }
    } else if (!main) {
      throw new ParseError(
        "language.missing_result_target",
        `${context} requires one target line.`,
        spanForLine(first),
      );
    }

    if (context === "exact_target" && main && !isCanonicalTarget(main.target)) {
      throw new ParseError(
        "language.incomplete_result_target",
        "exact_target requires a canonical exact Target.",
        spanForLine(first),
      );
    }
    if (context === "domain_root" && main
      && !(main.target.domain === "asset" && !("path" in main.target))) {
      throw new ParseError(
        "language.invalid_domain_root_target",
        "domain_root currently requires target { domain: asset }.",
        spanForLine(first),
      );
    }

    const section = lines[sectionIndex];
    const objectLines = lines.slice(sectionIndex + 1);
    if (section.text === "no_objects" && objectLines.length > 0) {
      throw new ParseError(
        "language.unexpected_object_text",
        "no_objects must terminate Result Text.",
        spanForLine(objectLines[0]),
      );
    }
    const aliases = new Set<string>();
    if (main) aliases.add(main.alias);
    for (const binding of related) aliases.add(binding.alias);
    const object = section.text === "objects"
      ? parseObjectText(objectLines, aliases)
      : undefined;

    if (context === "exact_target") {
      const result: ParsedResultText = {
        targetContext: context,
        target: main as CanonicalTargetBinding,
        ...(related.length ? { relatedTargets: related } : {}),
        ...(handoffs.length ? { handoffs } : {}),
        ...(object ? { object } : {}),
      };
      assertParsedResultContext(result, first);
      return {
        result,
        diagnostics: [],
      };
    }
    if (context === "domain_root") {
      const result: ParsedResultText = {
        targetContext: context,
        target: main as DomainRootTargetBinding,
        ...(related.length ? { relatedTargets: related } : {}),
        ...(handoffs.length ? { handoffs } : {}),
        ...(object ? { object } : {}),
      };
      assertParsedResultContext(result, first);
      return {
        result,
        diagnostics: [],
      };
    }
    const result: ParsedResultText = {
      targetContext: context,
      ...(object ? { object } : {}),
    };
    assertParsedResultContext(result, first);
    return {
      result,
      diagnostics: [],
    };
  } catch (error) {
    if (error instanceof ParseError) {
      return {
        diagnostics: [{ severity: "error", code: error.code, message: error.message, span: error.span }],
      };
    }
    throw error;
  }
}

function assertParsedResultContext(result: ParsedResultText, line: ParsedLine): void {
  const candidate = (result.targetContext === "unresolved_target"
    ? {
        ...result,
        diagnostics: [{
          severity: "error",
          code: "resolution.unresolved_target",
          message: "Result Text context placeholder.",
        }],
      }
    : { ...result, diagnostics: [] }) as ObjectResult;
  if (!isObjectResultContextSafe(candidate)) {
    throw new ParseError(
      "language.invalid_result_context",
      "Result Target table, handoffs, or object references violate contextual scope rules.",
      spanForLine(line),
    );
  }
}

function parseLeadingTargets(lines: ParsedLine[], options: ParseSalOptions): ParsedPrelude {
  const targets = new Map<string, QueryTargetBinding>();
  const legacyCalls = new Map<string, LegacyCall>();
  for (const line of lines) {
    if (line.kind === "comment") {
      continue;
    }
    const eq = findTopLevel(line.text, "=");
    const alias = eq < 0 ? "" : line.text.slice(0, eq).trim();
    const valueText = eq < 0 ? "" : line.text.slice(eq + 1).trim();
    if (!isLocalIdentifier(alias)) {
      throw new ParseError("language.invalid_target_binding", "Only local bindings may precede Query or Patch.", spanForLine(line));
    }
    if (targets.has(alias) || legacyCalls.has(alias)) {
      throw new ParseError("language.duplicate_binding", `Duplicate binding ${alias}.`, spanForLine(line));
    }
    if (valueText.startsWith("target")) {
      targets.set(alias, { alias, target: parseTargetExpression(valueText, line) });
      continue;
    }
    if (options.compatibility === "legacy") {
      const call = tryParseCall(valueText, line, new Set([...targets.keys(), ...legacyCalls.keys()]), {
        compatibility: "legacy",
      });
      if (call && ["asset", "blueprint", "class", "graph"].includes(call.callee)) {
        legacyCalls.set(alias, call);
        continue;
      }
    }
    throw new ParseError(
      "language.invalid_target_binding",
      "Request preludes require alias = target { domain: ..., ... }; legacy Target constructors require the explicit compatibility path.",
      spanForLine(line),
    );
  }
  return { targets, legacyCalls };
}

function parseTargetExpression(text: string, line: ParsedLine): QueryTarget {
  const match = /^target\s+(\{[\s\S]*\})$/.exec(text.trim());
  if (!match) {
    throw new ParseError(
      "language.invalid_target",
      "Expected target { domain: ..., ... }.",
      spanForLine(line),
    );
  }
  const inner = unwrap(match[1], "{", "}", line);
  const rawFields = new Map<string, string>();
  for (const part of splitTopLevelExact(inner, ",")) {
    if (part.trim() === "") {
      throw new ParseError("language.invalid_target", "Target entries cannot be empty.", spanForLine(line));
    }
    const colon = findTopLevel(part, ":");
    if (colon < 0) {
      throw new ParseError("language.invalid_target", "Target entries must use key: value.", spanForLine(line));
    }
    const key = part.slice(0, colon).trim();
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(key)) {
      throw new ParseError("language.invalid_target_field", `Invalid Target field ${key}.`, spanForLine(line));
    }
    if (rawFields.has(key)) {
      throw new ParseError("language.duplicate_target_field", `Duplicate Target field ${key}.`, spanForLine(line));
    }
    rawFields.set(key, part.slice(colon + 1).trim());
  }

  const rawDomain = rawFields.get("domain");
  if (!rawDomain || !domainKeywords.has(rawDomain)) {
    throw new ParseError(
      "language.invalid_target_domain",
      "Target domain must be one of asset, blueprint, class, graph, state_tree, widget, level, pcg, or pcg_component.",
      spanForLine(line),
    );
  }
  const domain = rawDomain as QueryTarget["domain"];
  rawFields.delete("domain");

  const values = new Map<string, string>();
  for (const [key, rawValue] of rawFields) {
    if (!/^"(?:[^"\\]|\\.)*"$/.test(rawValue)) {
      throw new ParseError(
        "language.invalid_target_value",
        `Target field ${key} must be a quoted string.`,
        spanForLine(line),
      );
    }
    const value = parseQuotedString(rawValue, line);
    if (value.length === 0) {
      throw new ParseError("language.invalid_target_value", `Target field ${key} cannot be empty.`, spanForLine(line));
    }
    values.set(key, value);
  }

  const allowed = targetFields(domain);
  for (const key of values.keys()) {
    if (!allowed.has(key)) {
      throw new ParseError(
        "language.unknown_target_field",
        `Target domain ${domain} does not define field ${key}.`,
        spanForLine(line),
      );
    }
  }
  const guidFields = domain === "pcg_component" ? ["actorId"] : ["id", "blueprintId"];
  for (const guidField of guidFields) {
    const value = values.get(guidField);
    if (value !== undefined) {
      const canonical = canonicalGuid(value);
      if (!canonical) {
        throw new ParseError(
          "language.invalid_target_guid",
          `Target field ${guidField} must be a GUID.`,
          spanForLine(line),
        );
      }
      values.set(guidField, canonical);
    }
  }

  const required = (key: string): string => {
    const value = values.get(key);
    if (!value) {
      throw new ParseError(
        "language.incomplete_target",
        `Target domain ${domain} requires field ${key}.`,
        spanForLine(line),
      );
    }
    return value;
  };

  switch (domain) {
    case "asset": {
      const path = values.get("path");
      const type = values.get("type");
      if (!path && type) {
        throw new ParseError(
          "language.incomplete_target",
          "Asset Target field type requires path.",
          spanForLine(line),
        );
      }
      return path
        ? { kind: "target", domain, path, ...(type ? { type } : {}) }
        : { kind: "target", domain };
    }
    case "blueprint":
      return {
        kind: "target",
        domain,
        asset: required("asset"),
        ...(values.get("id") ? { id: values.get("id")! } : {}),
      };
    case "class":
      return { kind: "target", domain, path: required("path") };
    case "graph": {
      const id = values.get("id");
      const name = values.get("name");
      if (!id && !name) {
        throw new ParseError(
          "language.incomplete_target",
          "Graph Target requires id or name.",
          spanForLine(line),
        );
      }
      return {
        kind: "target",
        domain,
        asset: required("asset"),
        ...(values.get("blueprintId") ? { blueprintId: values.get("blueprintId")! } : {}),
        ...(id ? { id } : {}),
        ...(name ? { name } : {}),
      } as QueryTarget;
    }
    case "state_tree":
      return {
        kind: "target",
        domain,
        asset: required("asset"),
        ...(values.get("type") ? { type: values.get("type")! } : {}),
      };
    case "widget":
      return {
        kind: "target",
        domain,
        asset: required("asset"),
        ...(values.get("id") ? { id: values.get("id")! } : {}),
      };
    case "level":
    case "pcg":
      return {
        kind: "target",
        domain,
        asset: required("asset"),
        ...(values.get("type") ? { type: values.get("type")! } : {}),
      };
    case "pcg_component": {
      const source = required("source");
      if (source !== "native" && source !== "scs" && source !== "instance") {
        throw new ParseError(
          "language.invalid_target_value",
          "pcg_component Target field source must be native, scs, or instance.",
          spanForLine(line),
        );
      }
      return {
        kind: "target",
        domain,
        asset: required("asset"),
        actorId: required("actorId"),
        source,
        id: required("id"),
        type: required("type"),
      };
    }
  }
}

function targetFields(domain: string): ReadonlySet<string> {
  switch (domain) {
    case "asset": return new Set(["path", "type"]);
    case "blueprint": return new Set(["asset", "id"]);
    case "class": return new Set(["path"]);
    case "graph": return new Set(["asset", "blueprintId", "id", "name"]);
    case "state_tree": return new Set(["asset", "type"]);
    case "widget": return new Set(["asset", "id"]);
    case "level": return new Set(["asset", "type"]);
    case "pcg": return new Set(["asset", "type"]);
    case "pcg_component": return new Set(["asset", "actorId", "source", "id", "type"]);
    default: return new Set();
  }
}

function canonicalGuid(value: string): string | undefined {
  const compact = /^[0-9a-fA-F]{32}$/.test(value)
    ? value
    : /^([0-9a-fA-F]{8})-([0-9a-fA-F]{4})-([0-9a-fA-F]{4})-([0-9a-fA-F]{4})-([0-9a-fA-F]{12})$/.test(value)
      ? value.replaceAll("-", "")
      : undefined;
  if (!compact) return undefined;
  const lower = compact.toLowerCase();
  if (lower === "00000000000000000000000000000000") return undefined;
  return `${lower.slice(0, 8)}-${lower.slice(8, 12)}-${lower.slice(12, 16)}-${lower.slice(16, 20)}-${lower.slice(20)}`;
}

function isCanonicalTarget(target: QueryTarget): target is CanonicalTarget {
  switch (target.domain) {
    case "asset":
      return "path" in target && typeof target.type === "string";
    case "blueprint":
      return typeof target.id === "string";
    case "class":
      return true;
    case "graph":
      return "id" in target
        && typeof target.id === "string"
        && typeof target.blueprintId === "string"
        && !("name" in target);
    case "state_tree":
      return typeof target.type === "string";
    case "widget":
      return typeof target.id === "string";
    case "level":
    case "pcg":
      return typeof target.type === "string";
    case "pcg_component":
      return true;
  }
}

function isPatchTarget(target: QueryTarget): target is PatchTarget {
  return target.domain !== "pcg_component" && isCanonicalTarget(target);
}

function isCanonicalEditorTarget(target: QueryTarget): target is CanonicalEditorTarget {
  return (target.domain === "blueprint" || target.domain === "graph") && isCanonicalTarget(target);
}

function lowerLegacyTarget(
  alias: string,
  calls: ReadonlyMap<string, LegacyCall>,
  requestKind: "query" | "patch",
  _requestLines: readonly ParsedLine[],
  line: ParsedLine,
  legacyDomain?: QueryTarget["domain"],
): QueryTarget {
  const call = calls.get(alias);
  if (!call) {
    throw new ParseError("language.unknown_target", `Legacy Target ${alias} is not declared.`, spanForLine(line));
  }
  let target: QueryTarget;
  switch (call.callee) {
    case "asset": {
      assertLegacyTargetFields(call, new Set(["path", "type"]), line);
      const path = legacyOptionalString(call, "path", line);
      const type = legacyOptionalNameOrString(call, "type", line);
      if (!path && type) {
        throw legacyTargetError(line, "Legacy asset type cannot be lowered without path.");
      }
      if (!legacyDomain) {
        throw legacyTargetError(
          line,
          "Legacy asset(...) is ambiguous between Asset and StateTree Domains; select legacyDomain explicitly.",
        );
      }
      if (legacyDomain === "asset") {
        target = path
          ? { kind: "target", domain: "asset", path, ...(type ? { type } : {}) }
          : { kind: "target", domain: "asset" };
      } else if (legacyDomain === "state_tree" && path) {
        target = { kind: "target", domain: "state_tree", asset: path, ...(type ? { type } : {}) };
      } else {
        throw legacyTargetError(line, `Legacy asset(...) cannot be lowered to ${legacyDomain} Domain.`);
      }
      break;
    }
    case "blueprint": {
      assertLegacyTargetFields(call, new Set(["asset", "id"]), line);
      const asset = resolveLegacyAsset(call.args.asset, calls, line);
      const id = legacyOptionalGuid(call, "id", line);
      if (!legacyDomain) {
        throw legacyTargetError(
          line,
          "Legacy blueprint(...) is ambiguous between Blueprint and Widget Domains; select legacyDomain explicitly.",
        );
      }
      if (legacyDomain === "blueprint") {
        target = { kind: "target", domain: "blueprint", asset, ...(id ? { id } : {}) };
      } else if (legacyDomain === "widget") {
        target = { kind: "target", domain: "widget", asset, ...(id ? { id } : {}) };
      } else {
        throw legacyTargetError(line, `Legacy blueprint(...) cannot be lowered to ${legacyDomain} Domain.`);
      }
      break;
    }
    case "class":
      assertLegacyTargetFields(call, new Set(["path"]), line);
      if (legacyDomain && legacyDomain !== "class") {
        throw legacyTargetError(line, `Legacy class(...) cannot be lowered to ${legacyDomain} Domain.`);
      }
      target = {
        kind: "target",
        domain: "class",
        path: legacyRequiredString(call, "path", line),
      };
      break;
    case "graph": {
      assertLegacyTargetFields(call, new Set(["asset", "id", "name"]), line);
      if (legacyDomain && legacyDomain !== "graph") {
        throw legacyTargetError(line, `Legacy graph(...) cannot be lowered to ${legacyDomain} Domain.`);
      }
      const owner = resolveLegacyGraphOwner(call.args.asset, calls, line);
      const id = legacyOptionalGuid(call, "id", line);
      const name = legacyOptionalNameOrString(call, "name", line);
      if (!id && !name) {
        throw legacyTargetError(line, "Legacy graph Target requires id or name.");
      }
      target = {
        kind: "target",
        domain: "graph",
        asset: owner.asset,
        ...(owner.blueprintId ? { blueprintId: owner.blueprintId } : {}),
        ...(id ? { id } : {}),
        ...(name ? { name } : {}),
      } as QueryTarget;
      break;
    }
    default:
      throw legacyTargetError(line, `Unsupported legacy Target constructor ${call.callee}.`);
  }
  if (requestKind === "patch" && !isCanonicalTarget(target)) {
    throw new ParseError(
      "language.incomplete_patch_target",
      "Legacy Target cannot be safely lowered to the canonical exact Target required by Patch.",
      spanForLine(line),
    );
  }
  return target;
}

function resolveLegacyAsset(
  value: LegacyCall["args"][string] | undefined,
  calls: ReadonlyMap<string, LegacyCall>,
  line: ParsedLine,
): string {
  if (typeof value === "string" && value.length > 0) return value;
  if (isLocalRef(value)) {
    const owner = calls.get(value.name);
    if (owner?.callee === "asset") {
      assertLegacyTargetFields(owner, new Set(["path", "type"]), line);
      rejectLegacyAssertionField(owner, "type", line);
      return legacyRequiredString(owner, "path", line);
    }
  }
  throw legacyTargetError(line, "Legacy Target owner cannot be safely projected to an asset path.");
}

function resolveLegacyGraphOwner(
  value: LegacyCall["args"][string] | undefined,
  calls: ReadonlyMap<string, LegacyCall>,
  line: ParsedLine,
): { asset: string; blueprintId?: string } {
  if (typeof value === "string" && value.length > 0) return { asset: value };
  if (!isLocalRef(value)) {
    throw legacyTargetError(line, "Legacy graph owner must resolve to an asset or blueprint Target.");
  }
  const owner = calls.get(value.name);
  if (!owner) {
    throw legacyTargetError(line, `Legacy graph owner ${value.name} is not declared.`);
  }
  if (owner.callee === "blueprint") {
    assertLegacyTargetFields(owner, new Set(["asset", "id"]), line);
    return {
      asset: resolveLegacyAsset(owner.args.asset, calls, line),
      ...(legacyOptionalGuid(owner, "id", line)
        ? { blueprintId: legacyOptionalGuid(owner, "id", line)! }
        : {}),
    };
  }
  if (owner.callee === "asset") {
    assertLegacyTargetFields(owner, new Set(["path", "type"]), line);
    rejectLegacyAssertionField(owner, "type", line);
    return { asset: legacyRequiredString(owner, "path", line) };
  }
  throw legacyTargetError(line, `Legacy graph owner ${value.name} is not an asset or blueprint Target.`);
}

function legacyRequiredString(call: LegacyCall, key: string, line: ParsedLine): string {
  const value = legacyOptionalString(call, key, line);
  if (!value) throw legacyTargetError(line, `Legacy ${call.callee} Target requires string field ${key}.`);
  return value;
}

function legacyOptionalString(
  call: LegacyCall,
  key: string,
  line: ParsedLine,
): string | undefined {
  if (!Object.hasOwn(call.args, key)) return undefined;
  const value = call.args[key];
  if (typeof value === "string" && value.length > 0) return value;
  throw legacyTargetError(
    line,
    `Legacy ${call.callee} field ${key} must be a non-empty string.`,
  );
}

function legacyOptionalNameOrString(
  call: LegacyCall,
  key: string,
  line: ParsedLine,
): string | undefined {
  if (!Object.hasOwn(call.args, key)) return undefined;
  const value = call.args[key];
  if (typeof value === "string" && value.length > 0) return value;
  if (typeof value === "object"
    && value !== null
    && !Array.isArray(value)
    && value.kind === "name"
    && value.name.length > 0) {
    return value.name;
  }
  throw legacyTargetError(
    line,
    `Legacy ${call.callee} field ${key} must be a non-empty string or name.`,
  );
}

function legacyOptionalGuid(call: LegacyCall, key: string, line: ParsedLine): string | undefined {
  const value = legacyOptionalString(call, key, line);
  if (value === undefined) return undefined;
  const canonical = canonicalGuid(value);
  if (!canonical) throw legacyTargetError(line, `Legacy ${call.callee} field ${key} is not a GUID.`);
  return canonical;
}

function assertLegacyTargetFields(
  call: LegacyCall,
  allowed: ReadonlySet<string>,
  line: ParsedLine,
): void {
  const unknown = Object.keys(call.args).find((key) => !allowed.has(key));
  if (unknown) {
    throw legacyTargetError(
      line,
      `Legacy ${call.callee} Target has unknown field ${unknown}.`,
    );
  }
}

function rejectLegacyAssertionField(
  call: LegacyCall,
  key: string,
  line: ParsedLine,
): void {
  if (Object.hasOwn(call.args, key)) {
    throw legacyTargetError(
      line,
      `Legacy ${call.callee} field ${key} is an assertion that cannot be safely projected into the selected v3 Target.`,
    );
  }
}

function legacyTargetError(line: ParsedLine, message: string): ParseError {
  return new ParseError("language.unsafe_legacy_target", message, spanForLine(line));
}

function requestTarget(
  alias: string,
  prelude: ParsedPrelude,
  line: ParsedLine,
  requestKind: "query" | "patch",
  requestLines: ParsedLine[],
  options: ParseSalOptions,
): QueryTargetBinding | Patch["target"] {
  const explicit = prelude.targets.get(alias);
  if (explicit) {
    if (prelude.targets.size !== 1 || prelude.legacyCalls.size !== 0) {
      throw new ParseError(
        "language.multiple_request_targets",
        "A new-protocol request contains exactly one Target binding.",
        spanForLine(line),
      );
    }
    if (requestKind === "patch") {
      if (explicit.target.domain === "pcg_component") {
        throw new ParseError(
          "language.invalid_patch_target",
          "pcg_component is Query-only in this protocol version and cannot be selected by Patch.",
          spanForLine(line),
        );
      }
      if (!isPatchTarget(explicit.target)) {
        throw new ParseError(
          "language.incomplete_patch_target",
          "Patch requires the selected Domain's canonical exact Target.",
          spanForLine(line),
        );
      }
    }
    return explicit as QueryTargetBinding | Patch["target"];
  }

  if (options.compatibility === "legacy") {
    if (prelude.targets.size !== 0) {
      throw new ParseError(
        "language.multiple_request_targets",
        "A compatibility request cannot mix explicit v3 Targets with legacy Target declarations.",
        spanForLine(line),
      );
    }
    if (alias === "asset" && prelude.legacyCalls.size === 0 && requestKind === "query") {
      return { alias, target: { kind: "target", domain: "asset" } };
    }
    assertSelectedLegacyTargetClosure(
      alias,
      prelude.legacyCalls,
      line,
    );
    const target = lowerLegacyTarget(
      alias,
      prelude.legacyCalls,
      requestKind,
      requestLines,
      line,
      options.legacyDomain,
    );
    return { alias, target } as QueryTargetBinding | Patch["target"];
  }

  throw new ParseError("language.unknown_target", `Target ${alias} has no Domain Target binding.`, spanForLine(line));
}

function assertSelectedLegacyTargetClosure(
  selectedAlias: string,
  calls: ReadonlyMap<string, LegacyCall>,
  line: ParsedLine,
): void {
  const reachable = new Set<string>();
  const visiting = new Set<string>();
  const visitAlias = (alias: string): void => {
    if (reachable.has(alias)) return;
    if (visiting.has(alias)) {
      throw legacyTargetError(
        line,
        `Legacy Target dependency cycle includes ${alias}.`,
      );
    }
    const call = calls.get(alias);
    if (!call) return;
    visiting.add(alias);
    visitLegacyDependencies(call.args, calls, visitAlias);
    visiting.delete(alias);
    reachable.add(alias);
  };
  visitAlias(selectedAlias);

  const unused = [...calls.keys()].filter((alias) => !reachable.has(alias));
  if (unused.length > 0) {
    throw legacyTargetError(
      line,
      `Legacy request contains Target declarations outside the selected ${selectedAlias} dependency closure: ${unused.join(", ")}.`,
    );
  }
}

function visitLegacyDependencies(
  value: unknown,
  calls: ReadonlyMap<string, LegacyCall>,
  visitAlias: (alias: string) => void,
): void {
  if (Array.isArray(value)) {
    for (const item of value) visitLegacyDependencies(item, calls, visitAlias);
    return;
  }
  if (typeof value !== "object" || value === null) return;
  if ("kind" in value
    && value.kind === "local"
    && "name" in value
    && typeof value.name === "string"
    && calls.has(value.name)) {
    visitAlias(value.name);
    return;
  }
  for (const nested of Object.values(value)) {
    visitLegacyDependencies(nested, calls, visitAlias);
  }
}

function parseQuery(
  lines: ParsedLine[],
  queryIndex: number,
  prelude: ParsedPrelude,
  options: ParseSalOptions,
): Query {
  const header = lines[queryIndex];
  const match = /^query\s+([A-Za-z_][A-Za-z0-9_]*)$/.exec(header.text);
  if (!match || !isLocalIdentifier(match[1])) {
    throw new ParseError("language.invalid_query_header", "Expected query <target>.", spanForLine(header));
  }
  const body = lines.slice(queryIndex + 1).filter((line) => line.kind === "code");
  const hasPrimaryOperation = body.length > 0 && !isQueryClause(body[0].text);
  const target = requestTarget(match[1], prelude, header, "query", body, options) as QueryTargetBinding;
  const expressionOptions: ExpressionParseOptions = {
    ...options,
    requestTargetAlias: match[1],
    legacyDomain: target.target.domain,
  };

  const query: Query = {
    kind: "query",
    target,
    operation: hasPrimaryOperation
      ? parseQueryOperation(body[0], expressionOptions, target.target.domain)
      : { kind: "target" },
  };
  const aliases = new Set([match[1]]);
  assertQueryRefsKnown(query.operation, aliases, hasPrimaryOperation ? body[0] : header);
  for (const line of body.slice(hasPrimaryOperation ? 1 : 0)) {
    if (line.text.startsWith("where ")) {
      if (query.where) duplicateClause("where", line);
      query.where = parseCondition(line.text.slice(6), line, aliases, expressionOptions);
    } else if (line.text.startsWith("with ")) {
      if (query.with) duplicateClause("with", line);
      query.with = parseDetails(line.text.slice(5), line) as NonNullable<Query["with"]>;
    } else if (line.text.startsWith("order by ")) {
      if (query.orderBy) duplicateClause("order by", line);
      query.orderBy = parseOrderBy(line.text.slice(9), line) as NonNullable<Query["orderBy"]>;
    } else if (line.text.startsWith("page ")) {
      query.page = mergePage(query.page, parsePage(line), line);
    } else {
      throw new ParseError("language.unexpected_query_statement", `Unexpected Query statement: ${line.text}`, spanForLine(line));
    }
  }
  return query;
}

function parseQueryOperation(
  line: ParsedLine,
  options: ExpressionParseOptions,
  targetDomain: QueryTarget["domain"],
): QueryOperation {
  const text = line.text;
  const expressionOptions: ExpressionParseOptions = options;
  if (text === "target") {
    return { kind: "target" };
  }
  if (text === "summary") {
    return { kind: "summary" };
  }
  if (text.startsWith("references ")) {
    const match = /^references\s+to\s+(.+?)(?:\s+in\s+(project))?$/.exec(text);
    if (match) {
      return {
        kind: "references",
        target: referencesTarget(match[1], line, expressionOptions),
        ...(match[2] ? { scope: "project" as const } : {}),
      };
    }
  }
  if (text.startsWith("exec flow ") || text.startsWith("data flow ")) {
    const match = /^(exec|data)\s+flow\s+(from|to)\s+(.+?)(?:\s+depth\s+(\d+))?$/.exec(text);
    if (!match) operationError(line);
    return {
      kind: match![1] === "exec" ? "exec_flow" : "data_flow",
      direction: match![2] as "from" | "to",
      target: stableRef(match![3], line, expressionOptions),
      ...(match![4] ? { depth: positiveInteger(match![4], line) } : {}),
    };
  }
  if (text.startsWith("context ")) {
    const match = /^context\s+(.+?)(?:\s+depth\s+(\d+))?$/.exec(text);
    if (!match) operationError(line);
    return {
      kind: "context",
      target: stableRef(match![1], line, expressionOptions),
      ...(match![2] ? { depth: positiveInteger(match![2], line) } : {}),
    };
  }
  if (text === "tree" || text.startsWith("tree ")) {
    let rest = text.slice(4).trim();
    let root: StableRef | undefined;
    let depth: number | undefined;
    const depthMatch = /^(.*?)(?:\s+)?depth\s+(\d+)$/.exec(rest);
    if (depthMatch) {
      rest = depthMatch[1].trim();
      depth = positiveInteger(depthMatch[2], line);
    }
    if (rest !== "") root = stableRef(rest, line, expressionOptions);
    return { kind: "tree", ...(root ? { root } : {}), ...(depth ? { depth } : {}) };
  }
  if (text === "palette entries" || text.startsWith("palette entries ")) {
    let rest = text.slice("palette entries".length).trim();
    let pinContext: { direction: "from" | "to"; pin: StableRef } | undefined;
    let destination: RequestRef | undefined;
    const context = paletteContext(rest);
    if (context) {
      const ref = parseRef(context.ref, line, expressionOptions);
      if (context.direction === "from") {
        if (!isStableRef(ref)) {
          throw new ParseError(
            "language.expected_stable_reference",
            "Graph Palette from context requires a stable reference.",
            spanForLine(line),
          );
        }
        pinContext = { direction: context.direction, pin: ref };
      } else if (targetDomain === "state_tree") {
        destination = ref;
      } else {
        if (!isStableRef(ref)) {
          throw new ParseError(
            "language.expected_stable_reference",
            "Graph Palette to context requires a stable reference.",
            spanForLine(line),
          );
        }
        pinContext = { direction: context.direction, pin: ref };
      }
      rest = context.search;
    }
    const search = rest === "" ? undefined : quotedText(rest, line);
    return {
      kind: "palette_entries",
      ...(search ? { text: search } : {}),
      ...(pinContext ? { pinContext } : {}),
      ...(destination ? { to: destination } : {}),
    };
  }
  const palette = /^palette\s+@(\S+?)(?:\s+to\s+(.+))?$/.exec(text);
  if (palette) {
    return {
      kind: "palette",
      id: palette[1],
      ...(palette[2] ? { to: parseRef(palette[2], line, expressionOptions) } : {}),
    };
  }
  if (/^(?:[A-Za-z_][A-Za-z0-9_]*\s+)?(?:[A-Za-z_][A-Za-z0-9_]*::)?@/.test(text)) {
    const exact = tryStableRef(text, line, expressionOptions);
    if (exact) return { kind: "object", target: exact };
  }
  const firstSpace = text.indexOf(" ");
  const kind = firstSpace < 0 ? text : text.slice(0, firstSpace);
  const rest = firstSpace < 0 ? "" : text.slice(firstSpace + 1).trim();
  if (collectionKinds.has(kind)) {
    return { kind, ...(rest ? { text: quotedText(rest, line) } : {}) } as QueryOperation;
  }
  if (namedKinds.has(kind) && rest !== "") {
    return { kind, name: exactName(rest, line) } as QueryOperation;
  }
  return operationError(line);
}

function parsePatch(
  lines: ParsedLine[],
  patchIndex: number,
  prelude: ParsedPrelude,
  options: ParseSalOptions,
): Patch {
  const header = lines[patchIndex];
  const match = /^patch\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+(dry run))?$/.exec(header.text);
  if (!match || !isLocalIdentifier(match[1])) {
    throw new ParseError("language.invalid_patch_header", "Expected patch <target> [dry run].", spanForLine(header));
  }
  const requestLines = lines.slice(patchIndex + 1).filter((line) => line.kind === "code");
  const target = requestTarget(
    match[1],
    prelude,
    header,
    "patch",
    requestLines,
    options,
  ) as Patch["target"];
  const aliases = new Set([match[1]]);
  const expressionOptions: ExpressionParseOptions = {
    ...options,
    requestTargetAlias: match[1],
    legacyDomain: target.target.domain,
  };
  const bindingTargets = new Set<string>();
  const statements: PatchStatement[] = [];
  for (const line of lines.slice(patchIndex + 1)) {
    if (line.kind === "comment") {
      continue;
    }
    const binding = tryParseBinding(line, aliases, expressionOptions);
    if (binding) {
      assertBindingOwnerKnown(binding.target, aliases, line);
      const key = bindingTargetKey(binding.target);
      if (bindingTargets.has(key)) {
        throw new ParseError("language.duplicate_binding", `Duplicate binding ${key}.`, spanForLine(line));
      }
      if (binding.target.kind === "local") {
        if (aliases.has(binding.target.name)) {
          throw new ParseError("language.duplicate_binding", `Duplicate binding ${binding.target.name}.`, spanForLine(line));
        }
        aliases.add(binding.target.name);
      }
      bindingTargets.add(key);
      statements.push(binding);
      continue;
    }
    const parsed = parsePatchOperation(line, aliases, expressionOptions);
    for (const statement of parsed.statements) {
      if (!isBindingStatement(statement)) {
        assertPatchRefsKnown(statement, aliases, line);
      }
    }
    statements.push(...parsed.statements);
    for (const alias of parsed.outputs) {
      if (aliases.has(alias)) {
        throw new ParseError("language.duplicate_binding", `Duplicate binding ${alias}.`, spanForLine(line));
      }
      aliases.add(alias);
    }
  }
  if (statements.length === 0) {
    throw new ParseError("language.missing_patch_statement", "Patch requires at least one binding or operation.", spanForLine(header));
  }
  return {
    kind: "patch",
    target,
    dryRun: Boolean(match[2]),
    statements: statements as Patch["statements"],
  };
}

function parsePatchOperation(
  line: ParsedLine,
  aliases: ReadonlySet<string>,
  options: ExpressionParseOptions,
): { statements: PatchStatement[]; outputs: string[] } {
  const text = line.text;
  if (text === "save" || text === "compile") {
    return { statements: [{ kind: text } as PatchOperation], outputs: [] };
  }
  if (text.startsWith("add ")) {
    return { statements: parseAdd(text.slice(4), line, options), outputs: [] };
  }
  if (text.startsWith("remove ")) {
    return simpleTarget("remove", text.slice(7), line, options);
  }
  if (text.startsWith("break ")) {
    return simpleTarget("break", text.slice(6), line, options);
  }
  if (text.startsWith("set ")) {
    const body = text.slice(4);
    const eq = findTopLevel(body, "=");
    if (eq < 0) operationError(line);
    const target = memberRef(body.slice(0, eq), line, options);
    return {
      statements: [{ kind: "set", target, value: parseExpr(body.slice(eq + 1), line, aliases, options) }],
      outputs: [],
    };
  }
  if (text.startsWith("reset ")) {
    return { statements: [{ kind: "reset", target: memberRef(text.slice(6), line, options) }], outputs: [] };
  }
  if (text.startsWith("move ")) {
    return { statements: [parseMove(text.slice(5), line, options)], outputs: [] };
  }
  if (text.startsWith("connect ") || text.startsWith("disconnect ")) {
    const kind = text.startsWith("connect ") ? "connect" : "disconnect";
    const body = text.slice(kind.length + 1);
    const edge = parseEdge(body, line, options);
    return { statements: [{ kind, ...edge }], outputs: [] };
  }
  if (text.startsWith("bind ") || text.startsWith("unbind ")) {
    const kind = text.startsWith("bind ") ? "bind" : "unbind";
    return { statements: [{ kind, ...parseEdge(text.slice(kind.length + 1), line, options) }], outputs: [] };
  }
  if (text.startsWith("insert ")) {
    const parts = splitTopLevelExact(text.slice(7), "->");
    if (parts.length !== 3) operationError(line);
    let middle = splitTopLevelExact(parts[1], " / ");
    if (middle.length !== 2) {
      middle = splitTopLevelExact(parts[1], "/");
    }
    if (middle.length !== 2) operationError(line);
    return {
      statements: [{
        kind: "insert",
        from: parseRef(parts[0], line, options),
        input: parseRef(middle[0], line, options),
        output: parseRef(middle[1], line, options),
        to: parseRef(parts[2], line, options),
      }],
      outputs: [],
    };
  }
  if (text.startsWith("wrap ")) {
    return { statements: [parseWrap(text.slice(5), line, options)], outputs: [] };
  }
  if (text.startsWith("replace ")) {
    const body = text.slice(8);
    const separator = findTopLevel(body, " with ");
    if (separator < 0) operationError(line);
    return {
      statements: [{
        kind: "replace",
        target: parseRef(body.slice(0, separator), line, options),
        with: parseRef(body.slice(separator + " with ".length), line, options),
      }],
      outputs: [],
    };
  }
  if (text.startsWith("invoke ")) {
    return parseInvoke(text.slice(7), line, aliases, options);
  }
  return operationError(line);
}

function parseAdd(text: string, line: ParsedLine, options: ExpressionParseOptions): PatchStatement[] {
  const space = text.indexOf(" ");
  const targetText = space < 0 ? text : text.slice(0, space);
  const rest = space < 0 ? "" : text.slice(space + 1).trim();
  const target = parseBindingTarget(targetText, line);
  const add: PatchOperation = { kind: "add", target };
  if (rest === "") {
    return [add];
  }
  const placement = /^(to|before|after)\s+(.+)$/.exec(rest);
  if (placement) {
    return [{ ...add, [placement[1]]: parseRef(placement[2], line, options) } as PatchOperation];
  }
  return [add, { kind: "connect", ...parseEdge(rest, line, options) }];
}

function simpleTarget(kind: "remove" | "break", text: string, line: ParsedLine, options: ExpressionParseOptions) {
  return { statements: [{ kind, target: parseRef(text, line, options) } as PatchOperation], outputs: [] };
}

function parseMove(text: string, line: ParsedLine, options: ExpressionParseOptions): PatchOperation {
  const destinations = ([" to ", " by ", " before ", " after "] as const)
    .map((token) => ({ token, index: findTopLevel(text, token) }))
    .filter((candidate) => candidate.index >= 0)
    .sort((left, right) => left.index - right.index);
  const destination = destinations[0];
  if (!destination) return operationError(line);

  const targetText = text.slice(0, destination.index).trim();
  const destinationText = text.slice(destination.index + destination.token.length).trim();
  if (targetText === "" || destinationText === "") return operationError(line);
  const target = parseRef(targetText, line, options);
  const mode = destination.token.trim() as "to" | "by" | "before" | "after";
  if (mode === "by") {
    return { kind: "move", target, by: parsePoint(destinationText, line) };
  }
  if (mode === "to" && (destinationText.startsWith("(") || destinationText.startsWith("["))) {
    return { kind: "move", target, to: parsePoint(destinationText, line) };
  }
  return { kind: "move", target, [mode]: parseRef(destinationText, line, options) } as PatchOperation;
}

function parseWrap(text: string, line: ParsedLine, options: ExpressionParseOptions): PatchOperation {
  const match = /^(.+)\s+with\s+([A-Za-z_][A-Za-z0-9_]*)$/.exec(text);
  if (!match) return operationError(line);
  const source = match[1].trim();
  const targets = source.startsWith("[")
    ? splitTopLevelExact(unwrap(source, "[", "]", line), ",").map((item) => parseRef(item, line, options))
    : [parseRef(source, line, options)];
  return { kind: "wrap", targets: targets as [RequestRef, ...RequestRef[]], with: localOutput(match[2], line) };
}

function parseInvoke(
  text: string,
  line: ParsedLine,
  aliases: ReadonlySet<string>,
  options: ExpressionParseOptions,
): { statements: PatchStatement[]; outputs: string[] } {
  const asIndex = findTopLevel(text, " as ");
  const invocationText = (asIndex < 0 ? text : text.slice(0, asIndex)).trim();
  const outputText = asIndex < 0 ? "" : text.slice(asIndex + 4).trim();
  const parts = splitTopLevelExact(invocationText, " ").filter((part) => part !== "");
  if (parts.length < 2) return operationError(line);
  const callText = parts.pop()!;
  const target = parseRef(parts.join(" "), line, options);
  const call = tryParseCall(callText, line, aliases, options);
  if (!call) return operationError(line);
  const outputs = outputText === "" ? [] : splitTopLevelExact(outputText, ",").map((item) => {
    const colon = findTopLevel(item, ":");
    const selector = colon < 0 ? undefined : item.slice(0, colon).trim();
    const alias = (colon < 0 ? item : item.slice(colon + 1)).trim();
    if (!isLocalIdentifier(alias) || (selector !== undefined && !/^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*$/.test(selector))) {
      throw new ParseError("language.invalid_invoke_output", "Invalid invoke output binding.", spanForLine(line));
    }
    return { ...(selector ? { selector } : {}), alias };
  });
  return {
    statements: [{ kind: "invoke", target, operation: call.callee, args: call.args, outputs }],
    outputs: outputs.map((output) => output.alias),
  };
}

function parseObjectText(
  lines: ParsedLine[],
  initialAliases: ReadonlySet<string> = new Set(),
  options: ParseSalOptions = {},
): ObjectText {
  const statements: ObjectText["statements"] = [];
  const aliases = new Set<string>(initialAliases);
  const bindingTargets = new Set<string>();
  for (const line of lines) {
    if (line.kind === "comment") {
      statements.push({ kind: "comment", text: line.text });
      continue;
    }
    const binding = tryParseResultBinding(line, aliases, options);
    if (binding) {
      assertBindingOwnerKnown(binding.target, aliases, line);
      const key = bindingTargetKey(binding.target);
      if (bindingTargets.has(key)) {
        throw new ParseError("language.duplicate_binding", `Duplicate binding ${key}.`, spanForLine(line));
      }
      if (binding.target.kind === "local" && aliases.has(binding.target.name)) {
        throw new ParseError("language.duplicate_binding", `Duplicate binding ${binding.target.name}.`, spanForLine(line));
      }
      statements.push(binding);
      bindingTargets.add(key);
      if (binding.target.kind === "local") {
        aliases.add(binding.target.name);
      }
      continue;
    }
    const parts = splitTopLevelExact(line.text, "->");
    if (parts.length === 2) {
      const from = parseResultRef(parts[0], line, options);
      const to = parseResultRef(parts[1], line, options);
      assertRefKnown(from, aliases, line);
      assertRefKnown(to, aliases, line);
      statements.push({ from, to });
      continue;
    }
    throw new ParseError("language.invalid_object_statement", `Unsupported Object Text statement: ${line.text}`, spanForLine(line));
  }
  return { statements };
}

function parseEdge(
  text: string,
  line: ParsedLine,
  options: ExpressionParseOptions,
): { from: RequestRef; to: RequestRef } {
  const parts = splitTopLevelExact(text, "->");
  if (parts.length !== 2) return operationError(line);
  return { from: parseRef(parts[0], line, options), to: parseRef(parts[1], line, options) };
}

function memberRef(text: string, line: ParsedLine, options: ExpressionParseOptions): RequestMemberRef {
  const ref = parseRef(text.trim(), line, options);
  if (!isMemberRef(ref)) {
    throw new ParseError("language.expected_member", "Expected an object field or member path.", spanForLine(line));
  }
  return ref;
}

function assertPatchRefsKnown(
  operation: PatchOperation,
  aliases: ReadonlySet<string>,
  line: ParsedLine,
): void {
  switch (operation.kind) {
    case "add":
      assertBindingTargetKnown(operation.target, aliases, line);
      if (operation.to) assertRefKnown(operation.to, aliases, line);
      if (operation.before) assertRefKnown(operation.before, aliases, line);
      if (operation.after) assertRefKnown(operation.after, aliases, line);
      return;
    case "remove":
    case "break":
      assertRefKnown(operation.target, aliases, line);
      return;
    case "set":
    case "reset":
      assertRefKnown(operation.target, aliases, line);
      return;
    case "move":
      assertRefKnown(operation.target, aliases, line);
      if (operation.to && !Array.isArray(operation.to)) assertRefKnown(operation.to, aliases, line);
      if (operation.before) assertRefKnown(operation.before, aliases, line);
      if (operation.after) assertRefKnown(operation.after, aliases, line);
      return;
    case "connect":
    case "disconnect":
    case "bind":
    case "unbind":
      assertRefKnown(operation.from, aliases, line);
      assertRefKnown(operation.to, aliases, line);
      return;
    case "insert":
      assertRefKnown(operation.from, aliases, line);
      assertRefKnown(operation.input, aliases, line);
      assertRefKnown(operation.output, aliases, line);
      assertRefKnown(operation.to, aliases, line);
      return;
    case "wrap":
      operation.targets.forEach((target) => assertRefKnown(target, aliases, line));
      assertRefKnown(operation.with, aliases, line);
      return;
    case "replace":
      assertRefKnown(operation.target, aliases, line);
      assertRefKnown(operation.with, aliases, line);
      return;
    case "invoke":
      assertRefKnown(operation.target, aliases, line);
      return;
    case "compile":
    case "save":
      return;
  }
}

function assertQueryRefsKnown(
  operation: QueryOperation,
  aliases: ReadonlySet<string>,
  line: ParsedLine,
): void {
  if ((operation.kind === "palette_entries" || operation.kind === "palette") && "to" in operation) {
    assertRefKnown(operation.to, aliases, line);
  }
}

function isQueryClause(text: string): boolean {
  return text.startsWith("where ")
    || text.startsWith("with ")
    || text.startsWith("order by ")
    || text.startsWith("page ");
}

function paletteContext(rest: string): { direction: "from" | "to"; search: string; ref: string } | undefined {
  let selected: { direction: "from" | "to"; index: number; refStart: number } | undefined;
  for (const direction of ["from", "to"] as const) {
    const prefix = `${direction} `;
    const token = ` ${direction} `;
    const index = rest.startsWith(prefix) ? 0 : findTopLevel(rest, token);
    if (index < 0 || (selected && selected.index <= index)) {
      continue;
    }
    selected = {
      direction,
      index,
      refStart: index + (index === 0 ? prefix.length : token.length),
    };
  }
  if (!selected) {
    return undefined;
  }
  return {
    direction: selected.direction,
    search: rest.slice(0, selected.index).trim(),
    ref: rest.slice(selected.refStart).trim(),
  };
}

function assertBindingTargetKnown(
  target: Binding["target"] | RequestBinding["target"],
  aliases: ReadonlySet<string>,
  line: ParsedLine,
): void {
  if (target.kind === "local") {
    assertLocalKnown(target.name, aliases, line);
    return;
  }
  assertLocalKnown(target.object.name, aliases, line);
}

function assertBindingOwnerKnown(
  target: Binding["target"] | RequestBinding["target"],
  aliases: ReadonlySet<string>,
  line: ParsedLine,
): void {
  if (target.kind === "member") {
    assertLocalKnown(target.object.name, aliases, line);
  }
}

function assertRefKnown(ref: RequestRef | ResultRef, aliases: ReadonlySet<string>, line: ParsedLine): void {
  if (isLocalRef(ref)) {
    assertLocalKnown(ref.name, aliases, line);
  } else if ("object" in ref && isLocalRef(ref.object)) {
    assertLocalKnown(ref.object.name, aliases, line);
  }
}

function assertLocalKnown(name: string, aliases: ReadonlySet<string>, line: ParsedLine): void {
  if (!aliases.has(name)) {
    throw new ParseError("language.unknown_local_reference", `Local alias ${name} has not been declared yet.`, spanForLine(line));
  }
}

function bindingTargetKey(target: Binding["target"] | RequestBinding["target"]): string {
  return target.kind === "local" ? target.name : `${target.object.name}.${target.path.join(".")}`;
}

function isBindingStatement(statement: PatchStatement): statement is RequestBinding {
  return !(("kind" in statement));
}

function stableRef(
  text: string,
  line: ParsedLine,
  options: ExpressionParseOptions = {},
): StableRef {
  const ref = parseRef(text, line, options);
  if (!isStableRef(ref)) {
    throw new ParseError("language.expected_stable_reference", "Expected a stable @identity reference.", spanForLine(line));
  }
  return ref;
}

function tryStableRef(
  text: string,
  line: ParsedLine,
  options: ExpressionParseOptions,
): StableRef | undefined {
  try {
    return stableRef(text, line, options);
  } catch (error) {
    if (error instanceof ParseError && error.code === "language.expected_stable_reference") return undefined;
    throw error;
  }
}

function referencesTarget(
  text: string,
  line: ParsedLine,
  options: ExpressionParseOptions,
): TargetSelfRef | TargetSelfMemberRef | StableRef | StableMemberRef {
  const trimmed = text.trim();
  if (trimmed === "target") return { kind: "target_self" };
  if (trimmed.startsWith("target." ) || trimmed.startsWith("target[")) {
    const suffix = trimmed.slice("target".length);
    const synthetic = parseRef(`target_alias${suffix}`, line, options);
    if (isMemberRef(synthetic) && isLocalRef(synthetic.object)) {
      return { kind: "member", object: { kind: "target_self" }, path: synthetic.path };
    }
  }
  const ref = parseRef(trimmed, line, options);
  if (isStableRef(ref)) {
    return ref;
  }
  if (isMemberRef(ref) && isStableRef(ref.object)) {
    return { kind: "member", object: ref.object, path: ref.path };
  }
  throw new ParseError(
    "language.expected_stable_reference",
    "Expected target, a target member, a stable @identity reference, or one of its member paths.",
    spanForLine(line),
  );
}

function localOutput(name: string, line: ParsedLine): LocalRef {
  if (!isLocalIdentifier(name)) {
    throw new ParseError("language.invalid_binding_target", `Invalid local alias ${name}.`, spanForLine(line));
  }
  return { kind: "local", name };
}

function positiveInteger(text: string, line: ParsedLine): number {
  const value = Number(text);
  if (!Number.isInteger(value) || value < 1) {
    throw new ParseError("language.invalid_depth", "Depth must be a positive integer.", spanForLine(line));
  }
  return value;
}

function quotedText(text: string, line: ParsedLine): string {
  if (!/^"(?:[^"\\]|\\.)*"$/.test(text)) {
    throw new ParseError("language.expected_quoted_text", "Search text must be quoted.", spanForLine(line));
  }
  return parseQuotedString(text, line);
}

function exactName(text: string, line: ParsedLine): string {
  if (/^"(?:[^"\\]|\\.)*"$/.test(text)) {
    return parseQuotedString(text, line);
  }
  if (!/^\S+$/.test(text)) {
    throw new ParseError("language.invalid_exact_name", "Quote exact names containing spaces.", spanForLine(line));
  }
  return text;
}

function parseQuotedString(text: string, line: ParsedLine): string {
  try {
    return JSON.parse(text) as string;
  } catch {
    throw new ParseError("language.invalid_string", "Invalid quoted string.", spanForLine(line));
  }
}

function mergePage(current: Page | undefined, next: Page, line: ParsedLine): Page {
  if ((next.limit !== undefined && current?.limit !== undefined) || (next.after !== undefined && current?.after !== undefined)) {
    duplicateClause("page", line);
  }
  return { ...current, ...next };
}

function duplicateClause(name: string, line: ParsedLine): never {
  throw new ParseError("language.duplicate_query_clause", `Duplicate ${name} clause.`, spanForLine(line));
}

function operationError(line: ParsedLine): never {
  throw new ParseError("language.invalid_operation", `Invalid operation: ${line.text}`, spanForLine(line));
}
