import type {
  LocalRef,
  Name,
  ObjectExpr,
  RequestExpr,
  RequestRef,
  ResultExpr,
  ResultObjectExpr,
  ResultRef,
  ScopedStableRef,
  SemanticTag,
  StableRef,
  QueryTarget,
} from "../index.js";
import {
  findTopLevel,
  ParseError,
  type ParsedLine,
  spanForLine,
  splitTopLevelExact,
  unwrap,
} from "./text.js";

export interface ExpressionParseOptions {
  compatibility?: "legacy";
  allowScopedRef?: boolean;
  requestTargetAlias?: string;
  legacyDomain?: QueryTarget["domain"];
}

export interface LegacyCall {
  kind: "legacy_call";
  callee: string;
  args: Record<string, RequestExpr>;
}

export const domainKeywords = new Set([
  "asset",
  "blueprint",
  "class",
  "graph",
  "state_tree",
  "widget",
  "level",
  "pcg",
  "pcg_component",
]);

export const reservedKeywords = new Set([
  "true",
  "false",
  "null",
  "target",
  "domain",
  "tree",
  "context",
  "palette",
  "object",
  ...domainKeywords,
]);

export function parseExpr(
  text: string,
  line: ParsedLine,
  aliases: ReadonlySet<string> = new Set(),
  options: ExpressionParseOptions = {},
): RequestExpr {
  return parseExpression(text, line, aliases, options, false) as RequestExpr;
}

export function parseResultExpr(
  text: string,
  line: ParsedLine,
  aliases: ReadonlySet<string> = new Set(),
  options: ExpressionParseOptions = {},
): ResultExpr {
  return parseExpression(text, line, aliases, { ...options, allowScopedRef: true }, true) as ResultExpr;
}

function parseExpression(
  text: string,
  line: ParsedLine,
  aliases: ReadonlySet<string>,
  options: ExpressionParseOptions,
  resultSide: boolean,
): RequestExpr | ResultExpr {
  const trimmed = text.trim();
  const legacyCall = tryParseCall(trimmed, line, aliases, options);
  if (legacyCall) {
    if (options.compatibility !== "legacy") {
      throw new ParseError(
        "language.legacy_constructor_requires_compatibility",
        "Constructor-like object values are legacy syntax; use {...} or enable the explicit legacy compatibility path.",
        spanForLine(line),
      );
    }
    return legacyCallToObject(legacyCall, line);
  }
  if (trimmed === "null") return null;
  if (trimmed === "true") return true;
  if (trimmed === "false") return false;
  if (/^-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?$/.test(trimmed)) {
    const value = Number(trimmed);
    if (!Number.isFinite(value)) {
      throw new ParseError(
        "language.invalid_number",
        "SAL numbers must be finite JSON numbers.",
        spanForLine(line),
      );
    }
    return value;
  }
  if (trimmed.startsWith('"') && trimmed.endsWith('"')) {
    return parseJsonString(trimmed, line);
  }
  if (trimmed.startsWith("[") && trimmed.endsWith("]")) {
    const inner = unwrap(trimmed, "[", "]", line);
    return inner.trim() === ""
      ? []
      : splitTopLevelExact(inner, ",").map((part) => parseExpression(part, line, aliases, options, resultSide));
  }

  if (/^target\s*\{/.test(trimmed)) {
    throw new ParseError(
      "language.target_not_object_expression",
      "target {...} is structural Target syntax and cannot appear as an ordinary object value.",
      spanForLine(line),
    );
  }
  const object = tryParseObjectExpression(trimmed, line, aliases, options, resultSide);
  if (object) return object;

  const taggedStable = /^([A-Za-z_][A-Za-z0-9_]*)\s+(?:[A-Za-z_][A-Za-z0-9_]*::)?@/.exec(trimmed);
  if (taggedStable && reservedKeywords.has(taggedStable[1])) {
    throw new ParseError(
      "language.reserved_semantic_tag",
      `${taggedStable[1]} is a structural SAL keyword and cannot be a semantic tag.`,
      spanForLine(line),
    );
  }
  const ref = tryParseRef(trimmed, aliases, false, options, line);
  if (ref) return ref;
  if (isIdentifier(trimmed) && !reservedKeywords.has(trimmed)) {
    return { kind: "name", name: trimmed };
  }
  throw new ParseError("language.unsupported_value", `Unsupported value: ${trimmed}`, spanForLine(line));
}

export function tryParseObjectExpr(
  text: string,
  line: ParsedLine,
  aliases: ReadonlySet<string> = new Set(),
  options: ExpressionParseOptions = {},
): ObjectExpr | undefined {
  return tryParseObjectExpression(text, line, aliases, options, false) as ObjectExpr | undefined;
}

function tryParseObjectExpression(
  text: string,
  line: ParsedLine,
  aliases: ReadonlySet<string>,
  options: ExpressionParseOptions,
  resultSide: boolean,
): ObjectExpr | ResultObjectExpr | undefined {
  const trimmed = text.trim();
  let semanticTag: SemanticTag | undefined;
  let objectText = trimmed;
  const tagged = /^([A-Za-z_][A-Za-z0-9_]*)\s+(\{[\s\S]*\})$/.exec(trimmed);
  if (tagged) {
    if (!isSemanticTag(tagged[1])) {
      throw new ParseError(
        "language.reserved_semantic_tag",
        `${tagged[1]} is a structural SAL keyword and cannot be a semantic tag.`,
        spanForLine(line),
      );
    }
    semanticTag = tagged[1];
    objectText = tagged[2];
  }
  if (!objectText.startsWith("{") || !objectText.endsWith("}")) {
    return undefined;
  }
  return {
    kind: "object",
    fields: parseObjectFields(objectText, line, aliases, options, resultSide),
    ...(semanticTag ? { semanticTag } : {}),
  };
}

export function tryParseCall(
  text: string,
  line: ParsedLine,
  aliases: ReadonlySet<string> = new Set(),
  options: ExpressionParseOptions = {},
): LegacyCall | undefined {
  const match = /^([A-Za-z_][A-Za-z0-9_]*)\(([\s\S]*)\)$/.exec(text.trim());
  if (!match) return undefined;
  return {
    kind: "legacy_call",
    callee: match[1],
    args: parseCallArgs(match[2], line, aliases, options),
  };
}

export function parseCallArgs(
  text: string,
  line: ParsedLine,
  aliases: ReadonlySet<string> = new Set(),
  options: ExpressionParseOptions = {},
): Record<string, RequestExpr> {
  if (text.trim() === "") return {};
  const result: Record<string, RequestExpr> = {};
  for (const part of splitTopLevelExact(text, ",")) {
    const colon = findTopLevel(part, ":");
    if (colon < 0) {
      throw new ParseError("language.invalid_call_args", "Arguments must use name: value.", spanForLine(line));
    }
    const name = part.slice(0, colon).trim();
    if (!isIdentifier(name)) {
      throw new ParseError("language.invalid_call_args", `Invalid argument name ${name}.`, spanForLine(line));
    }
    if (Object.hasOwn(result, name)) {
      throw new ParseError("language.duplicate_argument", `Duplicate argument ${name}.`, spanForLine(line));
    }
    Object.defineProperty(result, name, {
      value: parseExpr(part.slice(colon + 1), line, aliases, options),
      enumerable: true,
      configurable: true,
      writable: true,
    });
  }
  return result;
}

export function parseRef(
  text: string,
  line: ParsedLine,
  options: ExpressionParseOptions = {},
): RequestRef {
  assertNoReservedStableTag(text, line);
  const ref = tryParseRef(text, new Set(), true, options, line);
  if (!ref || isScopedStableRef(ref)) {
    throw new ParseError(
      "language.invalid_reference",
      `Expected a request reference, received ${text.trim()}.`,
      spanForLine(line),
    );
  }
  return ref as RequestRef;
}

export function parseResultRef(
  text: string,
  line: ParsedLine,
  options: ExpressionParseOptions = {},
): ResultRef {
  assertNoReservedStableTag(text, line);
  const ref = tryParseRef(text, new Set(), true, { ...options, allowScopedRef: true }, line);
  if (!ref) {
    throw new ParseError(
      "language.invalid_reference",
      `Expected a result reference, received ${text.trim()}.`,
      spanForLine(line),
    );
  }
  return ref as ResultRef;
}

function assertNoReservedStableTag(text: string, line: ParsedLine): void {
  const tagged = /^([A-Za-z_][A-Za-z0-9_]*)\s+(?:[A-Za-z_][A-Za-z0-9_]*::)?@/.exec(text.trim());
  if (tagged && reservedKeywords.has(tagged[1])) {
    throw new ParseError(
      "language.reserved_semantic_tag",
      `${tagged[1]} is a structural SAL keyword and cannot be a semantic tag.`,
      spanForLine(line),
    );
  }
}

export function tryParseRef(
  text: string,
  aliases: ReadonlySet<string> = new Set(),
  allowBareLocal = false,
  options: ExpressionParseOptions = {},
  line?: ParsedLine,
): RequestRef | ResultRef | undefined {
  const trimmed = text.trim();
  const stable = tryParseStableRef(trimmed, options);
  if (stable) return stable;

  if (options.compatibility === "legacy") {
    const legacy = /^([A-Za-z_][A-Za-z0-9_]*)@([^\.\[\]\s]+)((?:\.[A-Za-z_][A-Za-z0-9_]*|\[\d+\])*)$/.exec(trimmed);
    if (legacy) {
      const identityPath = lowerLegacyIdentityPath(
        legacy[1],
        legacy[2],
        options.legacyDomain,
        line,
      );
      if (!identityPath) return undefined;
      const object: StableRef = {
        kind: "stable_ref",
        identityPath,
        ...(isSemanticTag(legacy[1]) ? { semanticTag: legacy[1] } : {}),
      };
      if (!legacy[3]) return object;
      const path = parseMemberPath(legacy[3]);
      return path ? { kind: "member", object, path } : undefined;
    }
  }

  const local = /^([A-Za-z_][A-Za-z0-9_]*)((?:\.[A-Za-z_][A-Za-z0-9_]*|\[\d+\])*)$/.exec(trimmed);
  if (!local || (!allowBareLocal && !local[2] && !aliases.has(local[1]))) return undefined;
  if (!isLocalIdentifier(local[1])) return undefined;
  const object: LocalRef = { kind: "local", name: local[1] };
  if (!local[2]) return object;
  const path = parseMemberPath(local[2]);
  return path ? { kind: "member", object, path } : undefined;
}

function lowerLegacyIdentityPath(
  legacyKind: string,
  rawId: string,
  domain: QueryTarget["domain"] | undefined,
  line: ParsedLine | undefined,
): [string, ...string[]] | undefined {
  const reject = (reason: string): undefined => {
    if (!line) return undefined;
    throw new ParseError(
      "language.unsafe_legacy_reference",
      `Legacy reference ${legacyKind}@${rawId} cannot be lowered safely: ${reason}`,
      spanForLine(line),
    );
  };
  if (!domain) {
    return reject("select an active legacy Domain explicitly.");
  }

  if (legacyKind === domain && domain !== "widget") {
    return reject("a legacy Target-self spelling has no contained StableRef equivalent.");
  }

  let componentCount: 1 | 2;
  switch (domain) {
    case "asset":
    case "class":
      return reject(`${domain} Domain exposes no contained StableRefs.`);
    case "blueprint":
      if (["dispatcher", "graph", "component", "node"].includes(legacyKind)) {
        componentCount = 1;
      } else if (legacyKind === "variable") {
        componentCount = 2;
      } else {
        return reject(`${legacyKind} is not a safe ${domain} legacy reference kind.`);
      }
      break;
    case "graph":
      if (["node", "dispatcher", "component"].includes(legacyKind)) {
        componentCount = 1;
      } else if (legacyKind === "pin" || legacyKind === "variable") {
        componentCount = 2;
      } else {
        return reject(`${legacyKind} is not a safe ${domain} legacy reference kind.`);
      }
      break;
    case "state_tree":
      if (["state", "node", "transition", "object"].includes(legacyKind)) {
        componentCount = 1;
      } else if (legacyKind === "parameter") {
        componentCount = 2;
      } else {
        return reject(`${legacyKind} is not a safe ${domain} legacy reference kind.`);
      }
      break;
    case "widget":
      if (legacyKind !== "widget") {
        return reject(`${legacyKind} is not a safe ${domain} legacy reference kind.`);
      }
      componentCount = 1;
      break;
    case "level":
    case "pcg":
    case "pcg_component":
      return reject(`${domain} Domain has no legacy StableRef projection.`);
  }

  const components = componentCount === 2 ? rawId.split("/") : [rawId];
  if (components.length !== componentCount) {
    return reject(`expected ${componentCount} native GUID component${componentCount === 1 ? "" : "s"}.`);
  }
  const canonical = components.map(canonicalLegacyGuid);
  if (canonical.some((component) => component === undefined)) {
    return reject("every identity component must be a non-zero GUID.");
  }
  return canonical as [string, ...string[]];
}

function canonicalLegacyGuid(value: string): string | undefined {
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

function tryParseStableRef(
  text: string,
  options: ExpressionParseOptions,
): StableRef | ResultRef | undefined {
  let rest = text;
  let semanticTag: SemanticTag | undefined;
  const tag = /^([A-Za-z_][A-Za-z0-9_]*)\s+(.+)$/.exec(rest);
  if (tag && (tag[2].startsWith("@") || tag[2].includes("::@"))) {
    if (!isSemanticTag(tag[1])) return undefined;
    semanticTag = tag[1];
    rest = tag[2];
  }

  let scope: string | undefined;
  const scoped = /^([A-Za-z_][A-Za-z0-9_]*)::/.exec(rest);
  if (scoped) {
    if (!isLocalIdentifier(scoped[1])) return undefined;
    if (options.allowScopedRef) {
      scope = scoped[1];
    } else if (options.requestTargetAlias !== scoped[1]) {
      return undefined;
    }
    rest = rest.slice(scoped[0].length);
  }
  if (!rest.startsWith("@")) return undefined;
  rest = rest.slice(1);

  const parsed = parseIdentityAndMember(rest);
  if (!parsed) return undefined;
  const reference: StableRef = {
    kind: "stable_ref",
    identityPath: parsed.identityPath,
    ...(semanticTag ? { semanticTag } : {}),
  };
  const base: StableRef | ScopedStableRef = scope
    ? {
        kind: "scoped_stable_ref",
        target: { kind: "local", name: scope },
        reference,
      }
    : reference;
  return parsed.memberPath
    ? { kind: "member", object: base, path: parsed.memberPath }
    : base;
}

function parseIdentityAndMember(text: string): {
  identityPath: [string, ...string[]];
  memberPath?: [string | number, ...(string | number)[]];
} | undefined {
  const identityPath: string[] = [];
  let index = 0;
  while (index < text.length) {
    let segment = "";
    if (text[index] === '"') {
      const parsed = scanJsonString(text, index);
      if (!parsed || typeof parsed.value !== "string" || parsed.value.length === 0) return undefined;
      segment = parsed.value;
      index = parsed.end;
    } else {
      const match = /^[^\/.\[\]\s"]+/.exec(text.slice(index));
      if (!match) return undefined;
      segment = match[0];
      index += match[0].length;
    }
    identityPath.push(segment);
    if (text[index] === "/") {
      index += 1;
      if (index >= text.length) return undefined;
      continue;
    }
    break;
  }
  if (identityPath.length === 0) return undefined;
  const memberText = text.slice(index);
  if (memberText === "") {
    return { identityPath: identityPath as [string, ...string[]] };
  }
  const memberPath = parseMemberPath(memberText);
  return memberPath
    ? { identityPath: identityPath as [string, ...string[]], memberPath }
    : undefined;
}

function scanJsonString(text: string, start: number): { value: unknown; end: number } | undefined {
  let escaped = false;
  for (let index = start + 1; index < text.length; index += 1) {
    const char = text[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (char === "\\") {
      escaped = true;
      continue;
    }
    if (char === '"') {
      const raw = text.slice(start, index + 1);
      try {
        return { value: JSON.parse(raw), end: index + 1 };
      } catch {
        return undefined;
      }
    }
  }
  return undefined;
}

function parseMemberPath(text: string): [string | number, ...(string | number)[]] | undefined {
  const parts: Array<string | number> = [];
  let index = 0;
  while (index < text.length) {
    const identifier = /^\.([A-Za-z_][A-Za-z0-9_]*)/.exec(text.slice(index));
    if (identifier) {
      parts.push(identifier[1]);
      index += identifier[0].length;
      continue;
    }
    const arrayIndex = /^\[(\d+)\]/.exec(text.slice(index));
    if (!arrayIndex) return undefined;
    const value = Number(arrayIndex[1]);
    if (!Number.isSafeInteger(value) || value > 2147483647) return undefined;
    parts.push(value);
    index += arrayIndex[0].length;
  }
  return parts.length > 0
    ? parts as [string | number, ...(string | number)[]]
    : undefined;
}

function parseObjectFields(
  text: string,
  line: ParsedLine,
  aliases: ReadonlySet<string>,
  options: ExpressionParseOptions,
  resultSide: boolean,
): Record<string, RequestExpr | ResultExpr> {
  const inner = unwrap(text, "{", "}", line);
  if (inner.trim() === "") return {};
  const result: Record<string, RequestExpr | ResultExpr> = {};
  for (const part of splitTopLevelExact(inner, ",")) {
    const colon = findTopLevel(part, ":");
    if (colon < 0) {
      throw new ParseError("language.invalid_object", "Object entries must use key: value.", spanForLine(line));
    }
    const rawKey = part.slice(0, colon).trim();
    const key = rawKey.startsWith('"')
      ? parseJsonString(rawKey, line)
      : isIdentifier(rawKey)
        ? rawKey
        : undefined;
    if (typeof key !== "string") {
      throw new ParseError("language.invalid_object", `Invalid object key ${rawKey}.`, spanForLine(line));
    }
    if (Object.hasOwn(result, key)) {
      throw new ParseError("language.duplicate_object_key", `Duplicate object key ${key}.`, spanForLine(line));
    }
    Object.defineProperty(result, key, {
      value: parseExpression(part.slice(colon + 1), line, aliases, options, resultSide),
      enumerable: true,
      configurable: true,
      writable: true,
    });
  }
  return result;
}

function legacyCallToObject(
  call: LegacyCall,
  line: ParsedLine,
): ObjectExpr {
  if (call.callee !== "object" && !isSemanticTag(call.callee)) {
    throw new ParseError(
      "language.unsafe_legacy_constructor",
      `Legacy constructor ${call.callee}(...) cannot preserve its meaning as an erasable semantic tag.`,
      spanForLine(line),
    );
  }
  return {
    kind: "object",
    fields: call.args as Record<string, RequestExpr>,
    ...(call.callee !== "object" && isSemanticTag(call.callee)
      ? { semanticTag: call.callee }
      : {}),
  };
}

export function parsePoint(text: string, line: ParsedLine): [number, number] {
  const trimmed = text.trim();
  const inner = trimmed.startsWith("(") && trimmed.endsWith(")")
    ? unwrap(trimmed, "(", ")", line)
    : unwrap(trimmed, "[", "]", line);
  const parts = splitTopLevelExact(inner, ",").map((part) => part === "" ? Number.NaN : Number(part));
  if (parts.length !== 2 || parts.some((value) => !Number.isFinite(value))) {
    throw new ParseError("language.invalid_point", "Expected a two-number point.", spanForLine(line));
  }
  return [parts[0], parts[1]];
}

export function formatExpr(expr: RequestExpr | ResultExpr): string {
  if (expr === null || typeof expr === "boolean") return String(expr);
  if (typeof expr === "number") return formatNumber(expr);
  if (typeof expr === "string") return JSON.stringify(expr);
  if (Array.isArray(expr)) return `[${expr.map((item) => formatExpr(item)).join(", ")}]`;
  if (isName(expr)) return expr.name;
  if (isRef(expr)) return formatRef(expr);
  if (isObjectExpr(expr)) {
    const tag = expr.semanticTag ? `${expr.semanticTag} ` : "";
    const fields = Object.entries(expr.fields)
      .map(([key, value]) => `${formatObjectKey(key)}: ${formatExpr(value)}`)
      .join(", ");
    return `${tag}{${fields}}`;
  }
  throw new Error("Unsupported normalized SAL expression.");
}

export function formatNumber(value: number): string {
  if (!Number.isFinite(value)) {
    throw new Error("Cannot format a non-finite number as SAL.");
  }
  return Object.is(value, -0) ? "-0" : String(value);
}

export function formatCall(call: LegacyCall): string {
  return `${call.callee}(${formatArgList(call.args as Record<string, RequestExpr>)})`;
}

export function formatArgList(args: Record<string, RequestExpr>): string {
  return Object.entries(args).map(([key, value]) => `${key}: ${formatExpr(value)}`).join(", ");
}

export function formatRef(ref: RequestRef | ResultRef): string {
  if (isLocalRef(ref)) return ref.name;
  if (isScopedStableRef(ref)) {
    const tag = ref.reference.semanticTag ? `${ref.reference.semanticTag} ` : "";
    return `${tag}${ref.target.name}::@${formatIdentityPath(ref.reference.identityPath)}`;
  }
  if (isMemberRef(ref)) {
    return `${formatRef(ref.object)}${formatMemberPath(ref.path)}`;
  }
  const tag = ref.semanticTag ? `${ref.semanticTag} ` : "";
  return `${tag}@${formatIdentityPath(ref.identityPath)}`;
}

export function formatMemberPath(path: readonly (string | number)[]): string {
  return path.map((segment) => typeof segment === "number" ? `[${segment}]` : `.${segment}`).join("");
}

export function formatIdentityPath(path: readonly string[]): string {
  return path.map((segment) => isBareIdentitySegment(segment) ? segment : JSON.stringify(segment)).join("/");
}

export function localRef(name: string): LocalRef {
  return { kind: "local", name };
}

export function nameValue(name: string): Name {
  return { kind: "name", name };
}

export function isIdentifier(value: string): boolean {
  return /^[A-Za-z_][A-Za-z0-9_]*$/.test(value);
}

export function isLocalIdentifier(value: string): boolean {
  return isIdentifier(value) && !reservedKeywords.has(value);
}

export function isSemanticTag(value: string): value is SemanticTag {
  return isIdentifier(value) && !reservedKeywords.has(value);
}

export function isCall(value: unknown): value is LegacyCall {
  return hasKind(value, "legacy_call") && "callee" in value && "args" in value;
}

export function isName(value: unknown): value is Name {
  return hasKind(value, "name") && "name" in value;
}

export function isLocalRef(value: unknown): value is LocalRef {
  return hasKind(value, "local") && "name" in value;
}

export function isScopedStableRef(value: unknown): value is Extract<ResultRef, { kind: "scoped_stable_ref" }> {
  return hasKind(value, "scoped_stable_ref") && "target" in value && "reference" in value;
}

export function isStableRef(value: unknown): value is StableRef {
  return hasKind(value, "stable_ref") && "identityPath" in value;
}

export function isMemberRef(value: unknown): value is Extract<RequestRef | ResultRef, { kind: "member" }> {
  return hasKind(value, "member") && "object" in value && "path" in value;
}

export function isRef(value: unknown): value is RequestRef | ResultRef {
  return isLocalRef(value) || isStableRef(value) || isScopedStableRef(value) || isMemberRef(value);
}

export function isObjectExpr(
  value: unknown,
): value is Extract<RequestExpr | ResultExpr, { kind: "object" }> {
  return hasKind(value, "object") && "fields" in value;
}

function formatObjectKey(key: string): string {
  return isIdentifier(key) ? key : JSON.stringify(key);
}

function isBareIdentitySegment(value: string): boolean {
  return /^[A-Za-z0-9_:+-]+$/.test(value);
}

function parseJsonString(text: string, line: ParsedLine): string {
  try {
    const value: unknown = JSON.parse(text);
    if (typeof value !== "string") throw new Error();
    return value;
  } catch {
    throw new ParseError("language.invalid_string", "Invalid quoted string.", spanForLine(line));
  }
}

function hasKind(value: unknown, kind: string): value is { kind: string } {
  return typeof value === "object"
    && value !== null
    && !Array.isArray(value)
    && "kind" in value
    && value.kind === kind;
}
