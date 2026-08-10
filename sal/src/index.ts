import type {
  Diagnostic,
  DomainRootTargetBinding,
  DomainRootQueryResult,
  ExactMutationResult,
  ExactQueryResult,
  CanonicalTargetBinding,
  ObjectResult,
  Patch,
  Query,
  QueryTarget,
  QueryTargetBinding,
  ResultPage,
  SalObject,
  TargetHandoff,
  UnresolvedMutationResult,
  UnresolvedQueryResult,
} from "./generated/sal-object-schema.js";

export type {
  Add,
  AndCondition,
  AssetPathTarget,
  AssetRootTarget,
  AssetTarget,
  Binding,
  BindingMemberRef,
  BindingTarget,
  Bind,
  BlueprintTarget,
  Break,
  CanonicalAssetTarget,
  CanonicalBlueprintTarget,
  CanonicalGraphTarget,
  CanonicalLevelTarget,
  CanonicalPcgTarget,
  CanonicalStateTreeTarget,
  CanonicalTarget,
  CanonicalTargetBinding,
  CanonicalWidgetTarget,
  ClassTarget,
  CollectionOperation,
  Comment,
  CompareCondition,
  Compile,
  Condition,
  Connect,
  ContainsCondition,
  Diagnostic,
  DiagnosticPath,
  Disconnect,
  DomainRootQueryResult,
  DomainRootTargetBinding,
  Edge,
  EqCondition,
  ExactMutationResult,
  ExactObjectOperation,
  ExactQueryResult,
  FieldPath,
  FlowOperation,
  GraphByIdTarget,
  GraphByNameTarget,
  GraphTarget,
  GuidString,
  Insert,
  Invoke,
  InvokeOutput,
  LevelTarget,
  LocalIdentifier,
  LocalRef,
  Move,
  Name,
  NamedOperation,
  NeCondition,
  NotCondition,
  NonEmptyString,
  ObjectExpr,
  ObjectResult,
  ObjectText,
  OrderBy,
  OrCondition,
  Page,
  PaletteEntriesOperation,
  PaletteIdOperation,
  PalettePinContext,
  Patch,
  PatchOperation,
  PatchStatement,
  PatchTarget,
  PatchTargetBinding,
  PcgComponentTarget,
  PcgTarget,
  Point,
  Query,
  QueryOperation,
  QueryTarget,
  QueryTargetBinding,
  RequestBinding,
  RequestExpr,
  RequestMemberRef,
  RequestRef,
  ReferencesOperation,
  Remove,
  Replace,
  Reset,
  ResultExpr,
  ResultMemberRef,
  ResultObjectExpr,
  ResultPage,
  ResultRef,
  SALNormalizedObjectSchema,
  SalObject,
  Save,
  ScopedStableRef,
  SemanticTag,
  Set,
  SourceSpan,
  StableRef,
  StableMemberRef,
  StateTreePaletteEntriesOperation,
  StateTreePaletteIdOperation,
  Statement,
  SummaryOperation,
  TargetOperation,
  TargetHandoff,
  TargetSelfMemberRef,
  TargetSelfRef,
  TreeOperation,
  Unbind,
  UnresolvedMutationResult,
  UnresolvedQueryResult,
  WidgetTarget,
  Wrap,
} from "./generated/sal-object-schema.js";

/** @deprecated Use QueryTarget to make request admissibility explicit. */
export type Target = QueryTarget;
/** @deprecated Use QueryTargetBinding to make request admissibility explicit. */
export type TargetBinding = QueryTargetBinding;

export type QueryResult = ExactQueryResult | DomainRootQueryResult | UnresolvedQueryResult;
export type PatchResult = ExactMutationResult | UnresolvedMutationResult;
export type ExactTargetResultContext = Pick<
  ExactQueryResult,
  "targetContext" | "target" | "relatedTargets" | "handoffs"
>;
export type DomainRootResultContext = Pick<
  DomainRootQueryResult,
  "targetContext" | "target" | "relatedTargets" | "handoffs"
>;
export type UnresolvedTargetResultContext = Pick<UnresolvedQueryResult, "targetContext">;

export type SalText = string;

export interface SalExecutionOptions {
  signal?: AbortSignal;
}

export interface ParseResult {
  object?: SalObject;
  diagnostics: Diagnostic[];
}

export interface TextResult {
  text?: SalText;
  diagnostics: Diagnostic[];
  targetContext?: ObjectResult["targetContext"];
  target?: CanonicalTargetBinding | DomainRootTargetBinding;
  relatedTargets?: CanonicalTargetBinding[];
  handoffs?: TargetHandoff[];
  page?: ResultPage;
  isError?: boolean;
  dryRun?: boolean;
  valid?: boolean;
  applied?: boolean;
  assetPath?: string;
  operation?: string;
  resolvedRefs?: unknown;
  planned?: unknown;
  diff?: unknown;
  previousRevision?: string;
  newRevision?: string;
}

export interface Sal {
  query(text: SalText, options?: SalExecutionOptions): Promise<TextResult>;
  patch(text: SalText, options?: SalExecutionOptions): Promise<TextResult>;
  schema(module?: string): Promise<TextResult>;
}

export interface SalInterface {
  readonly name: string;
  readonly description: string;
  readonly text: string;
}

export interface CreateSalOptions {
  executor: SalExecutor;
  catalog: readonly SalInterface[];
}

export interface SalExecutor {
  readonly interfaces: readonly string[];
  query(object: Query, options?: SalExecutionOptions): Promise<QueryResult>;
  patch?(object: Patch, options?: SalExecutionOptions): Promise<PatchResult>;
}

export { formatSalObject, formatTargetExpression } from "./formatter.js";
export {
  parseCanonicalTargetText,
  parseSalObject,
  parseSalResultText,
  type CanonicalEditorTarget,
  type ParseCanonicalTargetTextResult,
  type ParsedResultText,
  type ParseResultTextResult,
  type ParseSalOptions,
} from "./parser.js";
export { createSal } from "./sdk.js";
export {
  formatObjectResultText,
  objectResultToTextResult,
  unresolvedTextResult,
} from "./result.js";
export { validateObjectResult, validateSalObject } from "./schema-validator.js";
